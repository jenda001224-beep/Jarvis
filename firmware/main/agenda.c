#include "agenda.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

#define LINE_CAPACITY 1024
#define MAX_CANDIDATES 128
#define MAX_OVERRIDES 64
#define MAX_EXDATES 24
#define MAX_BYDAY 8
#define MAX_BYMONTHDAY 8
#define DAY_SECONDS 86400L
#define MARGIN_DAYS 2
#define STEP_LIMIT 60000

typedef enum {
    FREQ_NONE,
    FREQ_DAILY,
    FREQ_WEEKLY,
    FREQ_MONTHLY,
    FREQ_YEARLY,
} freq_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    bool date_only;
    bool utc;
    bool valid;
} stamp_t;

typedef struct {
    int weekday;
    int ordinal;
} byday_t;

typedef struct {
    freq_t freq;
    int interval;
    int count;
    bool has_until;
    time_t until;
    long until_day;
    int byday_count;
    byday_t byday[MAX_BYDAY];
    int bymonthday_count;
    int bymonthday[MAX_BYMONTHDAY];
    int bymonth;
    int setpos;
    int week_start;
} rule_t;

typedef struct {
    uint32_t uid;
    stamp_t start;
    stamp_t end;
    bool has_duration;
    long duration;
    bool cancelled;
    bool has_recurrence_id;
    time_t recurrence_id;
    rule_t rule;
    int exdate_count;
    time_t exdates[MAX_EXDATES];
    char title[AGENDA_TITLE];
    char place[AGENDA_PLACE];
} draft_t;

typedef struct {
    uint32_t uid;
    time_t original;
    bool generated;
    agenda_event_t event;
} candidate_t;

typedef struct {
    uint32_t uid;
    time_t original;
} override_t;

struct agenda_parser {
    time_t window_start;
    time_t window_end;
    char line[LINE_CAPACITY];
    size_t line_length;
    bool line_ended;
    bool in_event;
    int nested;
    draft_t draft;
    int candidate_count;
    candidate_t candidates[MAX_CANDIDATES];
    int override_count;
    override_t overrides[MAX_OVERRIDES];
};

typedef struct {
    agenda_parser_t *parser;
    const draft_t *draft;
    long first_day;
    long from_day;
    long stop_day;
    long until_day;
    long span_days;
    long duration;
    long end_offset;
    bool wall_end;
    int produced;
    int steps;
} expansion_t;

static long days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    long era = (year >= 0 ? year : year - 399) / 400;
    long year_of_era = year - era * 400;
    long day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    long day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

static void civil_from_days(long days, int *year, int *month, int *day)
{
    days += 719468;
    long era = (days >= 0 ? days : days - 146096) / 146097;
    long day_of_era = days - era * 146097;
    long year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    long day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    long shifted_month = (5 * day_of_year + 2) / 153;
    *day = (int) (day_of_year - (153 * shifted_month + 2) / 5 + 1);
    *month = (int) (shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    *year = (int) (year_of_era + era * 400 + (*month <= 2));
}

static int weekday_of(long day)
{
    long weekday = (day + 4) % 7;
    return (int) (weekday < 0 ? weekday + 7 : weekday);
}

static int month_length(int year, int month)
{
    static const int LENGTHS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return LENGTHS[month - 1];
}

static time_t to_time(long day, int hour, int minute, int second, bool utc)
{
    if (utc) {
        return (time_t) day * DAY_SECONDS + hour * 3600L + minute * 60L + second;
    }
    struct tm local = {0};
    int year;
    int month;
    int month_day;
    civil_from_days(day, &year, &month, &month_day);
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = month_day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;
    time_t moment = mktime(&local);
    time_t earlier = moment - 3600;
    struct tm check;
    localtime_r(&earlier, &check);
    if (check.tm_mday == month_day && check.tm_hour == hour && check.tm_min == minute) {
        return earlier;
    }
    return moment;
}

static long stamp_day(const stamp_t *stamp)
{
    return days_from_civil(stamp->year, stamp->month, stamp->day);
}

static time_t stamp_time(const stamp_t *stamp)
{
    return to_time(stamp_day(stamp), stamp->hour, stamp->minute, stamp->second, stamp->utc);
}

static long local_day(time_t moment)
{
    struct tm local;
    localtime_r(&moment, &local);
    return days_from_civil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

static int number(const char *text, int digits)
{
    int value = 0;
    for (int i = 0; i < digits; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return -1;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

static bool parse_stamp(const char *value, stamp_t *out)
{
    memset(out, 0, sizeof(*out));
    size_t length = strlen(value);
    if (length < 8) {
        return false;
    }
    out->year = number(value, 4);
    out->month = number(value + 4, 2);
    out->day = number(value + 6, 2);
    if (out->year < 1900 || out->year > 2200 || out->month < 1 || out->month > 12 || out->day < 1 ||
        out->day > month_length(out->year, out->month)) {
        return false;
    }
    if (length >= 15 && (value[8] == 'T' || value[8] == 't')) {
        out->hour = number(value + 9, 2);
        out->minute = number(value + 11, 2);
        out->second = number(value + 13, 2);
        if (out->hour < 0 || out->hour > 23 || out->minute < 0 || out->minute > 59 || out->second < 0 ||
            out->second > 60) {
            return false;
        }
        out->utc = value[15] == 'Z' || value[15] == 'z';
    } else {
        out->date_only = true;
    }
    out->valid = true;
    return true;
}

static bool parse_duration(const char *value, long *out)
{
    long total = 0;
    bool negative = false;
    bool in_time = false;
    if (*value == '+' || *value == '-') {
        negative = *value == '-';
        value++;
    }
    if (*value != 'P' && *value != 'p') {
        return false;
    }
    value++;
    while (*value) {
        if (*value == 'T' || *value == 't') {
            in_time = true;
            value++;
            continue;
        }
        char *end = NULL;
        long amount = strtol(value, &end, 10);
        if (end == value || amount < 0 || amount > 100000) {
            return false;
        }
        switch (*end) {
        case 'W':
        case 'w':
            total += amount * 7 * DAY_SECONDS;
            break;
        case 'D':
        case 'd':
            total += amount * DAY_SECONDS;
            break;
        case 'H':
        case 'h':
            total += amount * 3600;
            break;
        case 'M':
        case 'm':
            if (!in_time) {
                return false;
            }
            total += amount * 60;
            break;
        case 'S':
        case 's':
            total += amount;
            break;
        default:
            return false;
        }
        value = end + 1;
    }
    *out = negative ? 0 : total;
    return true;
}

static int weekday_code(const char *text)
{
    static const char *const CODES[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int i = 0; i < 7; i++) {
        if (strncasecmp(text, CODES[i], 2) == 0) {
            return i;
        }
    }
    return -1;
}

static void parse_rule(char *value, rule_t *rule)
{
    memset(rule, 0, sizeof(*rule));
    rule->interval = 1;
    rule->week_start = 1;
    char *outer = NULL;
    for (char *part = strtok_r(value, ";", &outer); part; part = strtok_r(NULL, ";", &outer)) {
        char *argument = strchr(part, '=');
        if (!argument) {
            continue;
        }
        *argument++ = 0;
        if (strcasecmp(part, "FREQ") == 0) {
            rule->freq = strcasecmp(argument, "DAILY") == 0     ? FREQ_DAILY
                         : strcasecmp(argument, "WEEKLY") == 0  ? FREQ_WEEKLY
                         : strcasecmp(argument, "MONTHLY") == 0 ? FREQ_MONTHLY
                         : strcasecmp(argument, "YEARLY") == 0  ? FREQ_YEARLY
                                                                : FREQ_NONE;
        } else if (strcasecmp(part, "INTERVAL") == 0) {
            int interval = atoi(argument);
            rule->interval = interval > 0 && interval < 10000 ? interval : 1;
        } else if (strcasecmp(part, "COUNT") == 0) {
            int count = atoi(argument);
            rule->count = count > 0 ? count : 0;
        } else if (strcasecmp(part, "UNTIL") == 0) {
            stamp_t until;
            if (parse_stamp(argument, &until)) {
                if (until.date_only) {
                    until.hour = 23;
                    until.minute = 59;
                    until.second = 59;
                }
                rule->until = stamp_time(&until);
                rule->until_day = days_from_civil(until.year, until.month, until.day);
                rule->has_until = true;
            }
        } else if (strcasecmp(part, "BYDAY") == 0) {
            char *inner = NULL;
            for (char *item = strtok_r(argument, ",", &inner); item && rule->byday_count < MAX_BYDAY;
                 item = strtok_r(NULL, ",", &inner)) {
                char *code = NULL;
                long ordinal = strtol(item, &code, 10);
                int weekday = weekday_code(code);
                if (weekday >= 0 && ordinal >= -5 && ordinal <= 5) {
                    rule->byday[rule->byday_count].weekday = weekday;
                    rule->byday[rule->byday_count].ordinal = (int) ordinal;
                    rule->byday_count++;
                }
            }
        } else if (strcasecmp(part, "BYMONTHDAY") == 0) {
            char *inner = NULL;
            for (char *item = strtok_r(argument, ",", &inner); item && rule->bymonthday_count < MAX_BYMONTHDAY;
                 item = strtok_r(NULL, ",", &inner)) {
                int day = atoi(item);
                if (day != 0 && day >= -31 && day <= 31) {
                    rule->bymonthday[rule->bymonthday_count++] = day;
                }
            }
        } else if (strcasecmp(part, "BYMONTH") == 0) {
            int month = atoi(argument);
            rule->bymonth = month >= 1 && month <= 12 ? month : 0;
        } else if (strcasecmp(part, "BYSETPOS") == 0) {
            int position = atoi(argument);
            rule->setpos = position >= -31 && position <= 31 ? position : 0;
        } else if (strcasecmp(part, "WKST") == 0) {
            int weekday = weekday_code(argument);
            rule->week_start = weekday >= 0 ? weekday : 1;
        }
    }
}

static size_t complete_length(const char *text, size_t length)
{
    size_t lead = length;
    while (lead > 0 && ((unsigned char) text[lead - 1] & 0xC0) == 0x80) {
        lead--;
    }
    if (lead == 0) {
        return 0;
    }
    unsigned char first = (unsigned char) text[lead - 1];
    size_t needed = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
    return length - (lead - 1) < needed ? lead - 1 : length;
}

static void copy_text(const char *value, const char *line_break, char *out, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; value[i]; i++) {
        char single[2] = {value[i], 0};
        const char *piece = single;
        if (value[i] == '\\' && value[i + 1]) {
            i++;
            if (value[i] == 'n' || value[i] == 'N') {
                piece = line_break;
            } else {
                single[0] = value[i];
            }
        }
        size_t piece_length = strlen(piece);
        if (used + piece_length >= capacity) {
            break;
        }
        memcpy(out + used, piece, piece_length);
        used += piece_length;
    }
    used = complete_length(out, used);
    while (used > 0 && (out[used - 1] == ' ' || out[used - 1] == ',')) {
        used--;
    }
    out[used] = 0;
}

static uint32_t hash_of(const char *text)
{
    uint32_t hash = 2166136261U;
    for (; *text; text++) {
        hash = (hash ^ (unsigned char) *text) * 16777619U;
    }
    return hash;
}

static bool near_window(const agenda_parser_t *parser, time_t moment)
{
    return moment >= parser->window_start - MARGIN_DAYS * DAY_SECONDS &&
           moment <= parser->window_end + MARGIN_DAYS * DAY_SECONDS;
}

bool agenda_overlaps(const agenda_event_t *event, time_t from, time_t to)
{
    if (event->end > event->start) {
        return event->start < to && event->end > from;
    }
    return event->start >= from && event->start < to;
}

static void consider(agenda_parser_t *parser, const draft_t *draft, time_t start, time_t end, bool generated)
{
    agenda_event_t event = {.start = start, .end = end, .all_day = draft->start.date_only};
    if (!agenda_overlaps(&event, parser->window_start, parser->window_end) ||
        parser->candidate_count >= MAX_CANDIDATES) {
        return;
    }
    candidate_t *candidate = &parser->candidates[parser->candidate_count++];
    candidate->uid = draft->uid;
    candidate->original = start;
    candidate->generated = generated;
    candidate->event = event;
    memcpy(candidate->event.title, draft->title, sizeof(draft->title));
    memcpy(candidate->event.place, draft->place, sizeof(draft->place));
}

static bool emit(expansion_t *expansion, long day)
{
    const draft_t *draft = expansion->draft;
    if (day < expansion->first_day) {
        return true;
    }
    if (draft->rule.count && expansion->produced >= draft->rule.count) {
        return false;
    }
    if (day > expansion->stop_day || day > expansion->until_day) {
        return false;
    }
    expansion->produced++;
    if (day < expansion->from_day) {
        return true;
    }
    time_t start = to_time(day, draft->start.hour, draft->start.minute, draft->start.second, draft->start.utc);
    if (draft->rule.has_until && (draft->start.date_only ? day > draft->rule.until_day : start > draft->rule.until)) {
        return false;
    }
    for (int i = 0; i < draft->exdate_count; i++) {
        if (draft->exdates[i] == start) {
            return true;
        }
    }
    time_t end = start + (time_t) expansion->duration;
    if (draft->start.date_only) {
        end = to_time(day + expansion->span_days, 0, 0, 0, false);
    } else if (expansion->wall_end) {
        end = to_time(day + expansion->end_offset, draft->end.hour, draft->end.minute, draft->end.second, false);
    }
    consider(expansion->parser, draft, start, end, true);
    return true;
}

static bool spend(expansion_t *expansion)
{
    return ++expansion->steps <= STEP_LIMIT;
}

static bool listed_weekday(const rule_t *rule, int weekday)
{
    for (int i = 0; i < rule->byday_count; i++) {
        if (rule->byday[i].weekday == weekday) {
            return true;
        }
    }
    return false;
}

static bool listed_month_day(const rule_t *rule, int day, int length)
{
    for (int i = 0; i < rule->bymonthday_count; i++) {
        int wanted = rule->bymonthday[i];
        if (wanted == day || (wanted < 0 && length + 1 + wanted == day)) {
            return true;
        }
    }
    return false;
}

static int month_days(const rule_t *rule, int year, int month, int fallback, int *out)
{
    int length = month_length(year, month);
    long first = days_from_civil(year, month, 1);
    int count = 0;
    if (rule->byday_count) {
        for (int day = 1; day <= length; day++) {
            int weekday = weekday_of(first + day - 1);
            int nth = (day - 1) / 7 + 1;
            int from_end = (length - day) / 7 + 1;
            bool match = false;
            for (int i = 0; i < rule->byday_count && !match; i++) {
                const byday_t *entry = &rule->byday[i];
                match = entry->weekday == weekday &&
                        (entry->ordinal == 0 || entry->ordinal == nth || entry->ordinal == -from_end);
            }
            if (match && rule->bymonthday_count) {
                match = listed_month_day(rule, day, length);
            }
            if (match) {
                out[count++] = day;
            }
        }
        if (rule->setpos) {
            int index = rule->setpos > 0 ? rule->setpos - 1 : count + rule->setpos;
            if (index < 0 || index >= count) {
                return 0;
            }
            out[0] = out[index];
            return 1;
        }
        return count;
    }
    if (rule->bymonthday_count) {
        for (int day = 1; day <= length; day++) {
            if (listed_month_day(rule, day, length)) {
                out[count++] = day;
            }
        }
        return count;
    }
    if (fallback <= length) {
        out[count++] = fallback;
    }
    return count;
}

static void expand_daily(expansion_t *expansion)
{
    const rule_t *rule = &expansion->draft->rule;
    for (long day = expansion->first_day; day <= expansion->stop_day && spend(expansion); day += rule->interval) {
        if (rule->byday_count && !listed_weekday(rule, weekday_of(day))) {
            continue;
        }
        if (!emit(expansion, day)) {
            return;
        }
    }
}

static void expand_weekly(expansion_t *expansion)
{
    const rule_t *rule = &expansion->draft->rule;
    int start_weekday = weekday_of(expansion->first_day);
    long week = expansion->first_day - ((start_weekday - rule->week_start + 7) % 7);
    for (; week <= expansion->stop_day && spend(expansion); week += 7L * rule->interval) {
        for (int offset = 0; offset < 7; offset++) {
            long day = week + offset;
            int weekday = weekday_of(day);
            bool match = rule->byday_count ? listed_weekday(rule, weekday) : weekday == start_weekday;
            if (match && !emit(expansion, day)) {
                return;
            }
        }
    }
}

static void expand_monthly(expansion_t *expansion)
{
    const draft_t *draft = expansion->draft;
    int year = draft->start.year;
    int month = draft->start.month;
    while (spend(expansion)) {
        long first = days_from_civil(year, month, 1);
        if (first > expansion->stop_day) {
            return;
        }
        int days[31];
        int count = month_days(&draft->rule, year, month, draft->start.day, days);
        for (int i = 0; i < count; i++) {
            if (!emit(expansion, first + days[i] - 1)) {
                return;
            }
        }
        month += draft->rule.interval;
        year += (month - 1) / 12;
        month = (month - 1) % 12 + 1;
    }
}

static void expand_yearly(expansion_t *expansion)
{
    const draft_t *draft = expansion->draft;
    int month = draft->rule.bymonth ? draft->rule.bymonth : draft->start.month;
    for (int year = draft->start.year; year <= 2200 && spend(expansion); year += draft->rule.interval) {
        long first = days_from_civil(year, month, 1);
        if (first > expansion->stop_day) {
            return;
        }
        int days[31];
        int count = month_days(&draft->rule, year, month, draft->start.day, days);
        for (int i = 0; i < count; i++) {
            if (!emit(expansion, first + days[i] - 1)) {
                return;
            }
        }
    }
}

static void finish_event(agenda_parser_t *parser)
{
    const draft_t *draft = &parser->draft;
    if (!draft->start.valid) {
        return;
    }
    if (draft->has_recurrence_id && near_window(parser, draft->recurrence_id) &&
        parser->override_count < MAX_OVERRIDES) {
        parser->overrides[parser->override_count].uid = draft->uid;
        parser->overrides[parser->override_count].original = draft->recurrence_id;
        parser->override_count++;
    }
    if (draft->cancelled) {
        return;
    }
    bool all_day = draft->start.date_only;
    long first_day = stamp_day(&draft->start);
    time_t start = stamp_time(&draft->start);
    long span_days = 1;
    long duration = 0;
    if (all_day) {
        if (draft->end.valid) {
            span_days = stamp_day(&draft->end) - first_day;
        } else if (draft->has_duration) {
            span_days = draft->duration / DAY_SECONDS;
        }
        if (span_days < 1) {
            span_days = 1;
        }
    } else if (draft->end.valid) {
        duration = (long) (stamp_time(&draft->end) - start);
    } else if (draft->has_duration) {
        duration = draft->duration;
    }
    if (duration < 0) {
        duration = 0;
    }
    if (draft->rule.freq == FREQ_NONE || draft->has_recurrence_id) {
        time_t end = all_day ? to_time(first_day + span_days, 0, 0, 0, false) : start + (time_t) duration;
        consider(parser, draft, start, end, false);
        return;
    }
    long covered_days = all_day ? span_days : duration / DAY_SECONDS + 1;
    expansion_t expansion = {
        .parser = parser,
        .draft = draft,
        .first_day = first_day,
        .from_day = local_day(parser->window_start) - MARGIN_DAYS - covered_days,
        .stop_day = local_day(parser->window_end) + MARGIN_DAYS,
        .until_day = draft->rule.has_until ? (long) (draft->rule.until / DAY_SECONDS) + MARGIN_DAYS : LONG_MAX,
        .span_days = span_days,
        .duration = duration,
        .end_offset = draft->end.valid ? stamp_day(&draft->end) - first_day : 0,
        .wall_end = !all_day && draft->end.valid && !draft->end.date_only && !draft->start.utc && !draft->end.utc &&
                    duration > 0,
    };
    switch (draft->rule.freq) {
    case FREQ_DAILY:
        expand_daily(&expansion);
        break;
    case FREQ_WEEKLY:
        expand_weekly(&expansion);
        break;
    case FREQ_MONTHLY:
        expand_monthly(&expansion);
        break;
    case FREQ_YEARLY:
        expand_yearly(&expansion);
        break;
    default:
        break;
    }
}

static void handle_property(agenda_parser_t *parser, const char *name, char *value)
{
    draft_t *draft = &parser->draft;
    stamp_t stamp;
    if (strcasecmp(name, "UID") == 0) {
        draft->uid = hash_of(value);
    } else if (strcasecmp(name, "DTSTART") == 0) {
        parse_stamp(value, &draft->start);
    } else if (strcasecmp(name, "DTEND") == 0) {
        parse_stamp(value, &draft->end);
    } else if (strcasecmp(name, "DURATION") == 0) {
        draft->has_duration = parse_duration(value, &draft->duration);
    } else if (strcasecmp(name, "SUMMARY") == 0) {
        copy_text(value, " ", draft->title, sizeof(draft->title));
    } else if (strcasecmp(name, "LOCATION") == 0) {
        copy_text(value, ", ", draft->place, sizeof(draft->place));
    } else if (strcasecmp(name, "STATUS") == 0) {
        draft->cancelled = strcasecmp(value, "CANCELLED") == 0;
    } else if (strcasecmp(name, "RRULE") == 0) {
        parse_rule(value, &draft->rule);
    } else if (strcasecmp(name, "RECURRENCE-ID") == 0) {
        if (parse_stamp(value, &stamp)) {
            draft->recurrence_id = stamp_time(&stamp);
            draft->has_recurrence_id = true;
        }
    } else if (strcasecmp(name, "EXDATE") == 0) {
        char *inner = NULL;
        for (char *item = strtok_r(value, ",", &inner); item; item = strtok_r(NULL, ",", &inner)) {
            if (parse_stamp(item, &stamp) && draft->exdate_count < MAX_EXDATES) {
                time_t moment = stamp_time(&stamp);
                if (near_window(parser, moment)) {
                    draft->exdates[draft->exdate_count++] = moment;
                }
            }
        }
    }
}

static void handle_line(agenda_parser_t *parser)
{
    char *line = parser->line;
    size_t colon = 0;
    bool quoted = false;
    for (; line[colon]; colon++) {
        if (line[colon] == '"') {
            quoted = !quoted;
        } else if (line[colon] == ':' && !quoted) {
            break;
        }
    }
    if (!line[colon]) {
        return;
    }
    line[colon] = 0;
    char *value = line + colon + 1;
    char *parameters = strchr(line, ';');
    if (parameters) {
        *parameters = 0;
    }
    if (strcasecmp(line, "BEGIN") == 0) {
        if (parser->in_event) {
            parser->nested++;
        } else if (strcasecmp(value, "VEVENT") == 0) {
            parser->in_event = true;
            parser->nested = 0;
            memset(&parser->draft, 0, sizeof(parser->draft));
        }
        return;
    }
    if (strcasecmp(line, "END") == 0) {
        if (!parser->in_event) {
            return;
        }
        if (parser->nested > 0) {
            parser->nested--;
            return;
        }
        if (strcasecmp(value, "VEVENT") == 0) {
            finish_event(parser);
        }
        parser->in_event = false;
        return;
    }
    if (parser->in_event && parser->nested == 0) {
        handle_property(parser, line, value);
    }
}

static void flush_line(agenda_parser_t *parser)
{
    parser->line[parser->line_length] = 0;
    if (parser->line_length) {
        handle_line(parser);
    }
    parser->line_length = 0;
}

size_t agenda_parser_size(void)
{
    return sizeof(agenda_parser_t);
}

void agenda_parser_begin(agenda_parser_t *parser, time_t window_start, time_t window_end)
{
    memset(parser, 0, sizeof(*parser));
    parser->window_start = window_start;
    parser->window_end = window_end;
}

void agenda_parser_feed(agenda_parser_t *parser, const char *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        if (parser->line_ended) {
            parser->line_ended = false;
            if (c == ' ' || c == '\t') {
                continue;
            }
            flush_line(parser);
        }
        if (c == '\n') {
            parser->line_ended = true;
        } else if (c != '\r' && parser->line_length < LINE_CAPACITY - 1) {
            parser->line[parser->line_length++] = c;
        }
    }
}

void agenda_parser_source_end(agenda_parser_t *parser)
{
    flush_line(parser);
    parser->line_ended = false;
    parser->in_event = false;
    parser->nested = 0;
}

static int compare_candidates(const void *left, const void *right)
{
    const candidate_t *a = left;
    const candidate_t *b = right;
    if (a->event.start != b->event.start) {
        return a->event.start < b->event.start ? -1 : 1;
    }
    return strcmp(a->event.title, b->event.title);
}

int agenda_parser_finish(agenda_parser_t *parser, agenda_event_t *out, int capacity)
{
    agenda_parser_source_end(parser);
    int kept = 0;
    for (int i = 0; i < parser->candidate_count; i++) {
        const candidate_t *candidate = &parser->candidates[i];
        bool replaced = false;
        for (int j = 0; j < parser->override_count && candidate->generated && !replaced; j++) {
            replaced = parser->overrides[j].uid == candidate->uid &&
                       parser->overrides[j].original == candidate->original;
        }
        if (!replaced) {
            parser->candidates[kept++] = *candidate;
        }
    }
    parser->candidate_count = kept;
    qsort(parser->candidates, (size_t) kept, sizeof(candidate_t), compare_candidates);
    int count = kept < capacity ? kept : capacity;
    for (int i = 0; i < count; i++) {
        out[i] = parser->candidates[i].event;
    }
    return count;
}

char *agenda_describe(const agenda_event_t *events, int count, time_t from, time_t to)
{
    static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "events");
    char text[48];
    for (int i = 0; i < count; i++) {
        const agenda_event_t *event = &events[i];
        if (!agenda_overlaps(event, from, to)) {
            continue;
        }
        struct tm start;
        struct tm end;
        localtime_r(&event->start, &start);
        localtime_r(&event->end, &end);
        cJSON *item = cJSON_CreateObject();
        snprintf(text, sizeof(text), "%04d-%02d-%02d", start.tm_year + 1900, start.tm_mon + 1, start.tm_mday);
        cJSON_AddStringToObject(item, "date", text);
        cJSON_AddStringToObject(item, "weekday", DAYS[start.tm_wday]);
        if (event->all_day) {
            cJSON_AddBoolToObject(item, "all_day", true);
            if (event->end - event->start > DAY_SECONDS + 7200) {
                time_t last = event->end - DAY_SECONDS / 2;
                localtime_r(&last, &end);
                snprintf(text, sizeof(text), "%04d-%02d-%02d", end.tm_year + 1900, end.tm_mon + 1, end.tm_mday);
                cJSON_AddStringToObject(item, "last_date", text);
            }
        } else {
            snprintf(text, sizeof(text), "%02d:%02d", start.tm_hour, start.tm_min);
            cJSON_AddStringToObject(item, "start", text);
            if (event->end > event->start) {
                snprintf(text, sizeof(text), "%02d:%02d", end.tm_hour, end.tm_min);
                cJSON_AddStringToObject(item, "end", text);
            }
        }
        cJSON_AddStringToObject(item, "title", event->title[0] ? event->title : "bez názvu");
        if (event->place[0]) {
            cJSON_AddStringToObject(item, "place", event->place);
        }
        cJSON_AddItemToArray(list, item);
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return printed;
}

size_t agenda_summary(const agenda_event_t *events, int count, time_t from, time_t to, char *out, size_t capacity)
{
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < count; i++) {
        const agenda_event_t *event = &events[i];
        if (!agenda_overlaps(event, from, to)) {
            continue;
        }
        char entry[AGENDA_TITLE + AGENDA_PLACE + 48];
        char when[48] = "celý den";
        if (!event->all_day) {
            struct tm start;
            localtime_r(&event->start, &start);
            if (event->start < from) {
                snprintf(when, sizeof(when), "pokračuje");
            } else {
                snprintf(when, sizeof(when), "%02d:%02d", start.tm_hour, start.tm_min);
            }
        }
        int length = snprintf(entry, sizeof(entry), "%s%s %s%s%s%s", used ? "; " : "", when,
                              event->title[0] ? event->title : "bez názvu", event->place[0] ? " (" : "",
                              event->place, event->place[0] ? ")" : "");
        if (length < 0 || used + (size_t) length >= capacity) {
            break;
        }
        memcpy(out + used, entry, (size_t) length + 1);
        used += (size_t) length;
    }
    return used;
}
