#include "journal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "textfold.h"

#define LOG_MAGIC 0x4C4A524AU
#define NOTES_MAGIC 0x4E4A524AU
#define HEADER_SIZE 8
#define NOTES_HEADER_SIZE 16
#define MAX_LINES_PER_SECTOR 340
#define MAX_TERMS 8
#define TERM_SIZE 32
#define FOLDED_MAX (JOURNAL_LINE_MAX + 40)
#define SEARCH_KEEP 24

typedef struct {
    uint32_t magic;
    uint32_t seq;
} log_header_t;

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t length;
    uint32_t crc;
} notes_header_t;

static uint32_t crc32_of(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static size_t log_offset(size_t sector)
{
    return (JOURNAL_NOTES_SECTORS + sector) * JOURNAL_SECTOR;
}

static bool read_header(journal_t *journal, size_t sector, uint32_t *seq)
{
    log_header_t header;
    if (!journal->flash.read(journal->flash.context, log_offset(sector), &header, sizeof(header))) {
        return false;
    }
    if (header.magic != LOG_MAGIC || header.seq == 0xFFFFFFFFU) {
        return false;
    }
    *seq = header.seq;
    return true;
}

static bool start_sector(journal_t *journal, size_t sector, uint32_t seq)
{
    if (!journal->flash.erase(journal->flash.context, log_offset(sector), JOURNAL_SECTOR)) {
        return false;
    }
    log_header_t header = {.magic = LOG_MAGIC, .seq = seq};
    if (!journal->flash.write(journal->flash.context, log_offset(sector), &header, sizeof(header))) {
        return false;
    }
    journal->head = sector;
    journal->seq = seq;
    journal->used = HEADER_SIZE;
    return true;
}

static bool notes_slot_valid(journal_t *journal, int slot, notes_header_t *header)
{
    size_t offset = (size_t) slot * JOURNAL_SECTOR;
    if (!journal->flash.read(journal->flash.context, offset, header, sizeof(*header)) || header->magic != NOTES_MAGIC ||
        header->length > JOURNAL_NOTES_MAX) {
        return false;
    }
    if (!journal->flash.read(journal->flash.context, offset + NOTES_HEADER_SIZE, journal->buffer, header->length)) {
        return false;
    }
    return crc32_of(journal->buffer, header->length) == header->crc;
}

bool journal_open(journal_t *journal, const journal_flash_t *flash)
{
    journal->flash = *flash;
    if (flash->size < (JOURNAL_NOTES_SECTORS + 2) * JOURNAL_SECTOR) {
        return false;
    }
    journal->sectors = flash->size / JOURNAL_SECTOR - JOURNAL_NOTES_SECTORS;

    journal->notes_slot = -1;
    journal->notes_seq = 0;
    for (int slot = 0; slot < JOURNAL_NOTES_SECTORS; slot++) {
        notes_header_t header;
        if (notes_slot_valid(journal, slot, &header) && (journal->notes_slot < 0 || header.seq > journal->notes_seq)) {
            journal->notes_slot = slot;
            journal->notes_seq = header.seq;
        }
    }

    bool found = false;
    uint32_t newest = 0;
    size_t head = 0;
    for (size_t sector = 0; sector < journal->sectors; sector++) {
        uint32_t seq;
        if (read_header(journal, sector, &seq) && (!found || seq > newest)) {
            found = true;
            newest = seq;
            head = sector;
        }
    }
    if (!found) {
        return start_sector(journal, 0, 1);
    }
    journal->head = head;
    journal->seq = newest;
    if (!flash->read(flash->context, log_offset(head), journal->buffer, JOURNAL_SECTOR)) {
        return false;
    }
    size_t used = JOURNAL_SECTOR;
    while (used > HEADER_SIZE && journal->buffer[used - 1] == 0xFF) {
        used--;
    }
    if (used > HEADER_SIZE && journal->buffer[used - 1] != '\n' && used < JOURNAL_SECTOR) {
        static const char NEWLINE = '\n';
        if (!flash->write(flash->context, log_offset(head) + used, &NEWLINE, 1)) {
            return false;
        }
        used++;
    }
    journal->used = used;
    return true;
}

static size_t clean_chunk(const char *text, char *out, size_t capacity, size_t *consumed)
{
    size_t length = 0;
    size_t position = 0;
    while (text[position] && length + 1 < capacity) {
        unsigned char c = (unsigned char) text[position];
        size_t width = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (length + width >= capacity) {
            break;
        }
        bool valid = c < 0x80 || width > 1;
        for (size_t i = 1; valid && i < width; i++) {
            valid = ((unsigned char) text[position + i] & 0xC0) == 0x80;
        }
        if (!valid) {
            position++;
            continue;
        }
        if (c < 0x20) {
            if (length > 0 && out[length - 1] != ' ') {
                out[length++] = ' ';
            }
            position++;
            continue;
        }
        memcpy(out + length, text + position, width);
        length += width;
        position += width;
    }
    out[length] = 0;
    *consumed = position;
    return length;
}

bool journal_append(journal_t *journal, int64_t when, char who, const char *text)
{
    char chunk[JOURNAL_LINE_MAX + 1];
    char line[JOURNAL_LINE_MAX + 40];
    bool any = false;
    while (*text) {
        size_t consumed = 0;
        size_t length = clean_chunk(text, chunk, sizeof(chunk), &consumed);
        if (consumed == 0) {
            break;
        }
        text += consumed;
        if (length == 0) {
            continue;
        }
        int size = snprintf(line, sizeof(line), "%lld %c %s\n", (long long) when, who, chunk);
        if (size <= 0 || (size_t) size >= sizeof(line)) {
            return false;
        }
        if (journal->used + (size_t) size > JOURNAL_SECTOR &&
            !start_sector(journal, (journal->head + 1) % journal->sectors, journal->seq + 1)) {
            return false;
        }
        if (!journal->flash.write(journal->flash.context, log_offset(journal->head) + journal->used, line, (size_t) size)) {
            return false;
        }
        journal->used += (size_t) size;
        any = true;
    }
    return any;
}

static bool parse_line(char *start, char *end, journal_line_t *line)
{
    char *c = start;
    if (c == end || *c < '0' || *c > '9') {
        return false;
    }
    int64_t when = 0;
    while (c < end && *c >= '0' && *c <= '9') {
        when = when * 10 + (*c - '0');
        c++;
    }
    if (end - c < 3 || c[0] != ' ' || c[1] < 'A' || c[1] > 'Z' || c[2] != ' ') {
        return false;
    }
    for (char *check = c; check < end; check++) {
        if ((unsigned char) *check == 0xFF || *check == 0) {
            return false;
        }
    }
    line->when = when;
    line->who = c[1];
    *end = 0;
    line->text = c + 3;
    return true;
}

typedef struct {
    uint16_t start;
    uint16_t end;
} span_t;

void journal_each(journal_t *journal, journal_visit_t visit, void *context)
{
    span_t spans[MAX_LINES_PER_SECTOR];
    size_t sector = journal->head;
    uint32_t seq = journal->seq;
    for (size_t walked = 0; walked < journal->sectors; walked++) {
        uint32_t stored;
        if (!read_header(journal, sector, &stored) || stored != seq) {
            return;
        }
        if (!journal->flash.read(journal->flash.context, log_offset(sector), journal->buffer, JOURNAL_SECTOR)) {
            return;
        }
        int count = 0;
        size_t position = HEADER_SIZE;
        while (position < JOURNAL_SECTOR && count < MAX_LINES_PER_SECTOR) {
            uint8_t *found = memchr(journal->buffer + position, '\n', JOURNAL_SECTOR - position);
            if (!found) {
                break;
            }
            size_t end = (size_t) (found - journal->buffer);
            size_t start = position;
            while (start < end && journal->buffer[start] == 0) {
                start++;
            }
            if (start < end) {
                spans[count].start = (uint16_t) start;
                spans[count].end = (uint16_t) end;
                count++;
            }
            position = end + 1;
        }
        for (int i = count - 1; i >= 0; i--) {
            journal_line_t line;
            char *start = (char *) journal->buffer + spans[i].start;
            char *end = (char *) journal->buffer + spans[i].end;
            if (parse_line(start, end, &line) &&
                !visit(context, &line, log_offset(sector) + spans[i].start, (size_t) (spans[i].end - spans[i].start + 1))) {
                return;
            }
        }
        sector = (sector + journal->sectors - 1) % journal->sectors;
        seq--;
        if (seq == 0) {
            return;
        }
    }
}

static const char *speaker(char who)
{
    return who == 'P' ? "pán" : who == 'J' ? "Jarvis" : "poznámka";
}

static size_t format_line(const journal_line_t *line, char *out, size_t capacity)
{
    time_t moment = (time_t) line->when;
    struct tm local;
    localtime_r(&moment, &local);
    int written = snprintf(out, capacity, "%04d-%02d-%02d %02d:%02d %s: %s\n", local.tm_year + 1900, local.tm_mon + 1,
                           local.tm_mday, local.tm_hour, local.tm_min, speaker(line->who), line->text);
    return written > 0 && (size_t) written < capacity ? (size_t) written : 0;
}

typedef struct {
    char *text;
    size_t limit;
    size_t used;
    int64_t after;
} gather_t;

static bool gather_visit(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    (void) offset;
    (void) length;
    gather_t *gather = context;
    if (line->when <= gather->after) {
        return false;
    }
    char formatted[JOURNAL_LINE_MAX + 80];
    size_t size = format_line(line, formatted, sizeof(formatted));
    if (size == 0 || gather->used + size >= gather->limit) {
        return false;
    }
    gather->used += size;
    memcpy(gather->text + gather->limit - gather->used, formatted, size);
    return true;
}

static size_t gather_newest(journal_t *journal, int64_t after, size_t limit, char *out, size_t capacity)
{
    if (capacity == 0) {
        return 0;
    }
    if (limit > capacity) {
        limit = capacity;
    }
    gather_t gather = {.text = out, .limit = limit, .after = after};
    journal_each(journal, gather_visit, &gather);
    memmove(out, out + limit - gather.used, gather.used);
    out[gather.used] = 0;
    return gather.used;
}

size_t journal_since(journal_t *journal, int64_t after, char *out, size_t capacity)
{
    return gather_newest(journal, after, capacity, out, capacity);
}

size_t journal_recent(journal_t *journal, size_t limit, char *out, size_t capacity)
{
    return gather_newest(journal, -1, limit, out, capacity);
}

typedef struct {
    char who;
    int64_t when;
} newest_t;

static bool newest_visit(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    (void) offset;
    (void) length;
    newest_t *newest = context;
    if (!newest->who || line->who == newest->who) {
        newest->when = line->when;
        return false;
    }
    return true;
}

int64_t journal_newest(journal_t *journal, char who)
{
    newest_t newest = {.who = who};
    journal_each(journal, newest_visit, &newest);
    return newest.when;
}

static bool count_visit(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    (void) line;
    (void) offset;
    (void) length;
    (*(int *) context)++;
    return true;
}

int journal_count(journal_t *journal)
{
    int count = 0;
    journal_each(journal, count_visit, &count);
    return count;
}

typedef struct {
    char terms[MAX_TERMS][TERM_SIZE];
    int term_count;
} query_t;

static void parse_query(const char *text, query_t *query)
{
    char folded[256];
    text_fold(text, folded, sizeof(folded));
    query->term_count = 0;
    char *c = folded;
    while (*c && query->term_count < MAX_TERMS) {
        while (*c && !((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) {
            c++;
        }
        char *start = c;
        while ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) {
            c++;
        }
        size_t length = (size_t) (c - start);
        if (length < 3) {
            continue;
        }
        size_t stem = length >= 6 ? length - 2 : length >= 4 ? length - 1 : length;
        if (stem >= TERM_SIZE) {
            stem = TERM_SIZE - 1;
        }
        memcpy(query->terms[query->term_count], start, stem);
        query->terms[query->term_count][stem] = 0;
        query->term_count++;
    }
}

static int score_text(const query_t *query, const char *text)
{
    char folded[FOLDED_MAX];
    text_fold(text, folded, sizeof(folded));
    int score = 0;
    for (int i = 0; i < query->term_count; i++) {
        for (const char *found = strstr(folded, query->terms[i]); found; found = strstr(found + 1, query->terms[i])) {
            char before = found == folded ? ' ' : found[-1];
            if (!((before >= 'a' && before <= 'z') || (before >= '0' && before <= '9'))) {
                score++;
                break;
            }
        }
    }
    return score;
}

typedef struct {
    int64_t when;
    char who;
    int score;
    size_t offset;
    size_t length;
    char text[JOURNAL_LINE_MAX + 1];
} kept_t;

typedef struct {
    const query_t *query;
    kept_t *kept;
    int count;
    int needed;
} searching_t;

static bool search_visit(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    searching_t *searching = context;
    int score = score_text(searching->query, line->text);
    if (score < searching->needed) {
        return true;
    }
    int slot = searching->count;
    if (searching->count == SEARCH_KEEP) {
        slot = -1;
        for (int i = SEARCH_KEEP - 1; i >= 0; i--) {
            if (searching->kept[i].score < score && (slot < 0 || searching->kept[i].score < searching->kept[slot].score)) {
                slot = i;
            }
        }
        if (slot < 0) {
            return true;
        }
    } else {
        searching->count++;
    }
    kept_t *kept = &searching->kept[slot];
    kept->when = line->when;
    kept->who = line->who;
    kept->score = score;
    kept->offset = offset;
    kept->length = length;
    strncpy(kept->text, line->text, JOURNAL_LINE_MAX);
    kept->text[JOURNAL_LINE_MAX] = 0;
    return true;
}

static int compare_kept(const void *a, const void *b)
{
    const kept_t *first = a;
    const kept_t *second = b;
    if (first->score != second->score) {
        return second->score - first->score;
    }
    return first->when == second->when ? 0 : first->when > second->when ? -1 : 1;
}

int journal_search(journal_t *journal, const char *query_text, journal_visit_t visit, void *context, int limit)
{
    query_t query;
    parse_query(query_text, &query);
    if (query.term_count == 0) {
        return 0;
    }
    kept_t *kept = malloc(sizeof(kept_t) * SEARCH_KEEP);
    if (!kept) {
        return 0;
    }
    searching_t searching = {.query = &query, .kept = kept, .needed = (query.term_count + 1) / 2};
    journal_each(journal, search_visit, &searching);
    qsort(kept, (size_t) searching.count, sizeof(kept_t), compare_kept);
    int shown = 0;
    for (int i = 0; i < searching.count && shown < limit; i++) {
        journal_line_t line = {.when = kept[i].when, .who = kept[i].who, .text = kept[i].text};
        shown++;
        if (!visit(context, &line, kept[i].offset, kept[i].length)) {
            break;
        }
    }
    free(kept);
    return shown;
}

typedef struct {
    size_t offset;
    size_t length;
} doomed_t;

typedef struct {
    const query_t *query;
    doomed_t *doomed;
    int count;
    int capacity;
    int needed;
} forgetting_t;

static bool forget_visit(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    forgetting_t *forgetting = context;
    if (score_text(forgetting->query, line->text) >= forgetting->needed && forgetting->count < forgetting->capacity) {
        forgetting->doomed[forgetting->count].offset = offset;
        forgetting->doomed[forgetting->count].length = length;
        forgetting->count++;
    }
    return true;
}

int journal_forget(journal_t *journal, const char *query_text)
{
    query_t query;
    parse_query(query_text, &query);
    if (query.term_count == 0) {
        return 0;
    }
    enum { DOOMED_MAX = 512 };
    doomed_t *doomed = malloc(sizeof(doomed_t) * DOOMED_MAX);
    if (!doomed) {
        return 0;
    }
    forgetting_t forgetting = {.query = &query, .doomed = doomed, .capacity = DOOMED_MAX, .needed = query.term_count};
    journal_each(journal, forget_visit, &forgetting);
    static const uint8_t ZEROS[64];
    int removed = 0;
    for (int i = 0; i < forgetting.count; i++) {
        size_t left = doomed[i].length;
        size_t offset = doomed[i].offset;
        bool done = true;
        while (left > 0 && done) {
            size_t step = left < sizeof(ZEROS) ? left : sizeof(ZEROS);
            done = journal->flash.write(journal->flash.context, offset, ZEROS, step);
            offset += step;
            left -= step;
        }
        removed += done;
    }
    free(doomed);
    return removed;
}

bool journal_wipe(journal_t *journal)
{
    if (!journal->flash.erase(journal->flash.context, 0, journal->flash.size)) {
        return false;
    }
    journal->notes_slot = -1;
    journal->notes_seq = 0;
    return start_sector(journal, 0, 1);
}

size_t journal_notes_read(journal_t *journal, char *out, size_t capacity)
{
    if (capacity == 0) {
        return 0;
    }
    out[0] = 0;
    notes_header_t header;
    if (journal->notes_slot < 0 || !notes_slot_valid(journal, journal->notes_slot, &header)) {
        return 0;
    }
    size_t length = header.length < capacity - 1 ? header.length : capacity - 1;
    memcpy(out, journal->buffer, length);
    out[length] = 0;
    return length;
}

bool journal_notes_write(journal_t *journal, const char *text)
{
    size_t length = strlen(text);
    if (length > JOURNAL_NOTES_MAX) {
        length = JOURNAL_NOTES_MAX;
        while (length > 0 && ((unsigned char) text[length] & 0xC0) == 0x80) {
            length--;
        }
        size_t cut = length;
        while (cut > 0 && text[cut - 1] != '\n') {
            cut--;
        }
        if (cut > 0) {
            length = cut;
        }
    }
    int slot = journal->notes_slot == 0 ? 1 : 0;
    size_t offset = (size_t) slot * JOURNAL_SECTOR;
    notes_header_t header = {
        .magic = NOTES_MAGIC,
        .seq = journal->notes_seq + 1,
        .length = (uint32_t) length,
        .crc = crc32_of(text, length),
    };
    if (!journal->flash.erase(journal->flash.context, offset, JOURNAL_SECTOR)) {
        return false;
    }
    if (length > 0 && !journal->flash.write(journal->flash.context, offset + NOTES_HEADER_SIZE, text, length)) {
        return false;
    }
    if (!journal->flash.write(journal->flash.context, offset, &header, sizeof(header))) {
        return false;
    }
    journal->notes_slot = slot;
    journal->notes_seq = header.seq;
    return true;
}

bool journal_notes_add(journal_t *journal, const char *line)
{
    char *notes = malloc(JOURNAL_NOTES_MAX + JOURNAL_LINE_MAX + 8);
    if (!notes) {
        return false;
    }
    size_t length = journal_notes_read(journal, notes, JOURNAL_NOTES_MAX + 1);
    if (length > 0 && notes[length - 1] != '\n') {
        notes[length++] = '\n';
    }
    size_t consumed = 0;
    notes[length++] = '-';
    notes[length++] = ' ';
    length += clean_chunk(line, notes + length, JOURNAL_LINE_MAX + 1, &consumed);
    notes[length++] = '\n';
    notes[length] = 0;
    bool done = journal_notes_write(journal, notes);
    free(notes);
    return done;
}

int journal_notes_forget(journal_t *journal, const char *query_text)
{
    query_t query;
    parse_query(query_text, &query);
    if (query.term_count == 0) {
        return 0;
    }
    char *notes = malloc(JOURNAL_NOTES_MAX + 1);
    char *kept = malloc(JOURNAL_NOTES_MAX + 1);
    int removed = 0;
    if (notes && kept) {
        journal_notes_read(journal, notes, JOURNAL_NOTES_MAX + 1);
        size_t length = 0;
        char *line = notes;
        while (*line) {
            char *end = strchr(line, '\n');
            size_t size = end ? (size_t) (end - line) + 1 : strlen(line);
            char saved = line[size];
            line[size] = 0;
            if (score_text(&query, line) >= query.term_count) {
                removed++;
            } else {
                memcpy(kept + length, line, size);
                length += size;
            }
            line[size] = saved;
            line += size;
        }
        kept[length] = 0;
        if (removed > 0 && !journal_notes_write(journal, kept)) {
            removed = 0;
        }
    }
    free(notes);
    free(kept);
    return removed;
}
