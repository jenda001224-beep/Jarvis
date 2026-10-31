#include "dav.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LINE_OCTETS 75

const char DAV_PRINCIPAL_BODY[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";

const char DAV_HOME_BODY[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";

const char DAV_CALENDARS_BODY[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
    "<d:prop><d:displayname/><d:resourcetype/><c:supported-calendar-component-set/></d:prop></d:propfind>";

typedef struct {
    const char *name;
    size_t name_length;
    const char *attributes;
    const char *after;
    bool closing;
    bool empty;
} tag_t;

static bool next_tag(const char *from, const char *end, tag_t *tag)
{
    const char *c = from;
    for (;;) {
        c = memchr(c, '<', (size_t) (end - c));
        if (!c || c + 1 >= end) {
            return false;
        }
        if (c[1] == '?' || c[1] == '!') {
            c = memchr(c, '>', (size_t) (end - c));
            if (!c) {
                return false;
            }
            continue;
        }
        break;
    }
    c++;
    tag->closing = *c == '/';
    if (tag->closing) {
        c++;
    }
    const char *name = c;
    while (c < end && *c != '>' && *c != ' ' && *c != '/' && *c != '\t' && *c != '\n' && *c != '\r') {
        if (*c == ':') {
            name = c + 1;
        }
        c++;
    }
    tag->name = name;
    tag->name_length = (size_t) (c - name);
    tag->attributes = c;
    char quote = 0;
    while (c < end && (quote || *c != '>')) {
        if (quote) {
            quote = *c == quote ? 0 : quote;
        } else if (*c == '"' || *c == '\'') {
            quote = *c;
        }
        c++;
    }
    if (c >= end) {
        return false;
    }
    tag->empty = c[-1] == '/';
    tag->after = c + 1;
    return true;
}

static bool tag_is(const tag_t *tag, const char *name)
{
    return tag->name_length == strlen(name) && strncasecmp(tag->name, name, tag->name_length) == 0;
}

static void decode_text(const char *from, const char *to, char *out, size_t capacity)
{
    size_t used = 0;
    while (from < to && used + 1 < capacity) {
        if (*from == '&') {
            static const struct {
                const char *name;
                char value;
            } NAMES[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            bool found = false;
            for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
                size_t size = strlen(NAMES[i].name);
                if ((size_t) (to - from) >= size && strncmp(from, NAMES[i].name, size) == 0) {
                    out[used++] = NAMES[i].value;
                    from += size;
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }
        }
        out[used++] = *from++;
    }
    out[used] = 0;
}

static bool element_text(const tag_t *tag, const char *end, char *out, size_t capacity)
{
    const char *close = memchr(tag->after, '<', (size_t) (end - tag->after));
    if (!close) {
        return false;
    }
    const char *from = tag->after;
    while (from < close && (*from == ' ' || *from == '\n' || *from == '\r' || *from == '\t')) {
        from++;
    }
    const char *to = close;
    while (to > from && (to[-1] == ' ' || to[-1] == '\n' || to[-1] == '\r' || to[-1] == '\t')) {
        to--;
    }
    decode_text(from, to, out, capacity);
    return true;
}

bool dav_find_href(const char *xml, size_t length, const char *element, char *out, size_t capacity)
{
    const char *end = xml + length;
    const char *c = xml;
    tag_t tag;
    bool inside = false;
    while (next_tag(c, end, &tag)) {
        if (tag_is(&tag, element)) {
            if (tag.closing || tag.empty) {
                inside = false;
            } else {
                inside = true;
            }
        } else if (inside && !tag.closing && tag_is(&tag, "href")) {
            return element_text(&tag, end, out, capacity) && out[0];
        }
        c = tag.after;
    }
    return false;
}

int dav_parse_calendars(const char *xml, size_t length, dav_calendar_t *out, int capacity)
{
    const char *end = xml + length;
    const char *c = xml;
    tag_t tag;
    dav_calendar_t current = {0};
    bool in_response = false;
    bool in_type = false;
    bool is_calendar = false;
    bool has_components = false;
    bool has_events = false;
    int count = 0;
    while (next_tag(c, end, &tag)) {
        if (tag_is(&tag, "response")) {
            if (!tag.closing) {
                memset(&current, 0, sizeof(current));
                in_response = true;
                is_calendar = false;
                has_components = false;
                has_events = false;
            } else {
                in_response = false;
                if (is_calendar && (!has_components || has_events) && current.href[0] && count < capacity) {
                    out[count++] = current;
                }
            }
        } else if (in_response) {
            if (tag_is(&tag, "resourcetype")) {
                in_type = !tag.closing && !tag.empty;
            } else if (in_type && !tag.closing && tag_is(&tag, "calendar")) {
                is_calendar = true;
            } else if (!tag.closing && !tag.empty && tag_is(&tag, "href") && !current.href[0]) {
                element_text(&tag, end, current.href, sizeof(current.href));
            } else if (!tag.closing && !tag.empty && tag_is(&tag, "displayname")) {
                element_text(&tag, end, current.name, sizeof(current.name));
            } else if (!tag.closing && tag_is(&tag, "comp")) {
                has_components = true;
                size_t size = (size_t) (tag.after - tag.attributes);
                for (size_t i = 0; i + 6 <= size; i++) {
                    if (strncasecmp(tag.attributes + i, "VEVENT", 6) == 0) {
                        has_events = true;
                    }
                }
            }
        }
        c = tag.after;
    }
    return count;
}

enum {
    STREAM_TEXT,
    STREAM_TAG,
    STREAM_DATA,
    STREAM_DATA_TAG,
    STREAM_ENTITY,
    STREAM_CDATA,
};

static const char CDATA_OPEN[] = "![CDATA[";

void dav_stream_begin(dav_stream_t *stream, dav_data_t data, dav_end_t end, void *context)
{
    memset(stream, 0, sizeof(*stream));
    stream->data = data;
    stream->end = end;
    stream->context = context;
}

static void flush_pending(dav_stream_t *stream)
{
    if (stream->pending_length > 0) {
        stream->data(stream->context, stream->pending, (size_t) stream->pending_length);
        stream->pending_length = 0;
    }
}

static void emit(dav_stream_t *stream, char c)
{
    if (stream->pending_length == (int) sizeof(stream->pending)) {
        flush_pending(stream);
    }
    stream->pending[stream->pending_length++] = c;
}

static void finish_entity(dav_stream_t *stream)
{
    stream->entity[stream->entity_length] = 0;
    const char *name = stream->entity;
    long code = -1;
    if (strcmp(name, "amp") == 0) {
        code = '&';
    } else if (strcmp(name, "lt") == 0) {
        code = '<';
    } else if (strcmp(name, "gt") == 0) {
        code = '>';
    } else if (strcmp(name, "quot") == 0) {
        code = '"';
    } else if (strcmp(name, "apos") == 0) {
        code = '\'';
    } else if (name[0] == '#') {
        code = name[1] == 'x' || name[1] == 'X' ? strtol(name + 2, NULL, 16) : strtol(name + 1, NULL, 10);
    }
    if (code >= 0 && code < 0x80) {
        emit(stream, (char) code);
    } else if (code >= 0x80 && code < 0x800) {
        emit(stream, (char) (0xC0 | (code >> 6)));
        emit(stream, (char) (0x80 | (code & 0x3F)));
    } else if (code >= 0x800 && code < 0x10000) {
        emit(stream, (char) (0xE0 | (code >> 12)));
        emit(stream, (char) (0x80 | ((code >> 6) & 0x3F)));
        emit(stream, (char) (0x80 | (code & 0x3F)));
    }
}

static const char *local_name(const char *tag)
{
    const char *name = tag;
    for (const char *c = tag; *c && *c != ' '; c++) {
        if (*c == ':') {
            name = c + 1;
        }
    }
    return name;
}

static bool is_data_tag(const char *tag)
{
    const char *name = local_name(tag);
    size_t size = strlen("calendar-data");
    return strncasecmp(name, "calendar-data", size) == 0 && (name[size] == 0 || name[size] == ' ' || name[size] == '/');
}

void dav_stream_feed(dav_stream_t *stream, const char *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char c = data[i];
        switch (stream->state) {
        case STREAM_TEXT:
            if (c == '<') {
                stream->state = STREAM_TAG;
                stream->tag_length = 0;
            }
            break;
        case STREAM_TAG:
            if (c == '>') {
                stream->tag[stream->tag_length] = 0;
                bool opens = stream->tag[0] != '/' && stream->tag[0] != '?' && stream->tag[0] != '!' &&
                             (stream->tag_length == 0 || stream->tag[stream->tag_length - 1] != '/');
                stream->state = opens && is_data_tag(stream->tag) ? STREAM_DATA : STREAM_TEXT;
            } else if (stream->tag_length + 1 < (int) sizeof(stream->tag)) {
                stream->tag[stream->tag_length++] = c;
            }
            break;
        case STREAM_DATA:
            if (c == '<') {
                stream->state = STREAM_DATA_TAG;
                stream->tag_length = 0;
            } else if (c == '&') {
                stream->state = STREAM_ENTITY;
                stream->entity_length = 0;
            } else {
                emit(stream, c);
            }
            break;
        case STREAM_DATA_TAG:
            if (stream->tag_length < 8 && c == CDATA_OPEN[stream->tag_length]) {
                stream->tag_length++;
                if (stream->tag_length == 8) {
                    stream->state = STREAM_CDATA;
                    stream->cdata_end = 0;
                }
            } else {
                flush_pending(stream);
                if (stream->end) {
                    stream->end(stream->context);
                }
                stream->tag_length = 0;
                stream->tag[stream->tag_length++] = c;
                stream->state = c == '>' ? STREAM_TEXT : STREAM_TAG;
                if (c == '>') {
                    stream->tag_length = 0;
                }
            }
            break;
        case STREAM_ENTITY:
            if (c == ';') {
                finish_entity(stream);
                stream->state = STREAM_DATA;
            } else if (stream->entity_length + 1 < (int) sizeof(stream->entity)) {
                stream->entity[stream->entity_length++] = c;
            } else {
                stream->state = STREAM_DATA;
            }
            break;
        case STREAM_CDATA:
            if (c == ']' && stream->cdata_end < 2) {
                stream->cdata_end++;
            } else if (c == '>' && stream->cdata_end == 2) {
                stream->state = STREAM_DATA;
                stream->cdata_end = 0;
            } else {
                for (int k = 0; k < stream->cdata_end; k++) {
                    emit(stream, ']');
                }
                if (c == ']') {
                    stream->cdata_end = 1;
                } else {
                    stream->cdata_end = 0;
                    emit(stream, c);
                }
            }
            break;
        default:
            break;
        }
    }
}

void dav_stream_finish(dav_stream_t *stream)
{
    flush_pending(stream);
}

static void utc_stamp(time_t moment, char *out, size_t capacity)
{
    struct tm utc;
    gmtime_r(&moment, &utc);
    snprintf(out, capacity, "%04d%02d%02dT%02d%02d%02dZ", utc.tm_year + 1900, (utc.tm_mon + 1) % 100, utc.tm_mday % 100, utc.tm_hour % 100,
             utc.tm_min % 100, utc.tm_sec % 100);
}

char *dav_query_body(time_t from, time_t to)
{
    char start[40];
    char end[40];
    utc_stamp(from, start, sizeof(start));
    utc_stamp(to, end, sizeof(end));
    char *body = malloc(640);
    if (body) {
        snprintf(body, 640,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?><c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                 "<d:prop><c:calendar-data/></d:prop><c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
                 "<c:time-range start=\"%s\" end=\"%s\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>",
                 start, end);
    }
    return body;
}

typedef struct {
    char *data;
    size_t used;
    size_t capacity;
} out_t;

static bool grow(out_t *out, size_t more)
{
    if (out->used + more + 1 <= out->capacity) {
        return true;
    }
    size_t capacity = out->capacity ? out->capacity : 512;
    while (capacity < out->used + more + 1) {
        capacity *= 2;
    }
    char *grown = realloc(out->data, capacity);
    if (!grown) {
        return false;
    }
    out->data = grown;
    out->capacity = capacity;
    return true;
}

static void add_line(out_t *out, const char *name, const char *value, bool escape)
{
    size_t size = strlen(name) + strlen(value) * 2 + 16;
    size_t folds = size / (LINE_OCTETS - 1) + 2;
    if (!grow(out, size + folds * 3)) {
        return;
    }
    size_t octets = 0;
    for (const char *c = name; *c; c++) {
        out->data[out->used++] = *c;
        octets++;
    }
    out->data[out->used++] = ':';
    octets++;
    for (const unsigned char *c = (const unsigned char *) value; *c;) {
        char piece[5];
        size_t width = 0;
        if (escape && (*c == ',' || *c == ';' || *c == '\\')) {
            piece[width++] = '\\';
            piece[width++] = (char) *c;
            c++;
        } else if (escape && (*c == '\n' || *c == '\r')) {
            piece[width++] = '\\';
            piece[width++] = 'n';
            c++;
            while (*c == '\n' || *c == '\r') {
                c++;
            }
        } else {
            size_t bytes = *c < 0x80 ? 1 : (*c & 0xE0) == 0xC0 ? 2 : (*c & 0xF0) == 0xE0 ? 3 : (*c & 0xF8) == 0xF0 ? 4 : 1;
            for (size_t i = 0; i < bytes && c[i]; i++) {
                piece[width++] = (char) c[i];
            }
            c += width;
        }
        if (octets + width > LINE_OCTETS) {
            memcpy(out->data + out->used, "\r\n ", 3);
            out->used += 3;
            octets = 1;
        }
        memcpy(out->data + out->used, piece, width);
        out->used += width;
        octets += width;
    }
    memcpy(out->data + out->used, "\r\n", 2);
    out->used += 2;
    out->data[out->used] = 0;
}

static void local_date(time_t moment, char *out, size_t capacity)
{
    struct tm local;
    localtime_r(&moment, &local);
    snprintf(out, capacity, "%04d%02d%02d", local.tm_year + 1900, (local.tm_mon + 1) % 100, local.tm_mday % 100);
}

char *dav_build_event(const dav_event_t *event)
{
    out_t out = {0};
    char stamp[40];
    add_line(&out, "BEGIN", "VCALENDAR", false);
    add_line(&out, "VERSION", "2.0", false);
    add_line(&out, "PRODID", "-//Jarvis//Wall Assistant//CS", false);
    add_line(&out, "BEGIN", "VEVENT", false);
    add_line(&out, "UID", event->uid, false);
    utc_stamp(event->stamp, stamp, sizeof(stamp));
    add_line(&out, "DTSTAMP", stamp, false);
    if (event->all_day) {
        local_date(event->start, stamp, sizeof(stamp));
        add_line(&out, "DTSTART;VALUE=DATE", stamp, false);
        local_date(event->end, stamp, sizeof(stamp));
        add_line(&out, "DTEND;VALUE=DATE", stamp, false);
    } else {
        utc_stamp(event->start, stamp, sizeof(stamp));
        add_line(&out, "DTSTART", stamp, false);
        utc_stamp(event->end, stamp, sizeof(stamp));
        add_line(&out, "DTEND", stamp, false);
    }
    add_line(&out, "SUMMARY", event->title, true);
    if (event->place && event->place[0]) {
        add_line(&out, "LOCATION", event->place, true);
    }
    add_line(&out, "END", "VEVENT", false);
    add_line(&out, "END", "VCALENDAR", false);
    return out.data;
}

void dav_origin(const char *url, char *out, size_t capacity)
{
    const char *scheme = strstr(url, "://");
    const char *path = scheme ? strchr(scheme + 3, '/') : NULL;
    size_t length = path ? (size_t) (path - url) : strlen(url);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(out, url, length);
    out[length] = 0;
}

void dav_join_url(const char *origin, const char *href, char *out, size_t capacity)
{
    if (strncasecmp(href, "http://", 7) == 0 || strncasecmp(href, "https://", 8) == 0) {
        snprintf(out, capacity, "%s", href);
    } else {
        snprintf(out, capacity, "%s%s%s", origin, href[0] == '/' ? "" : "/", href);
    }
}
