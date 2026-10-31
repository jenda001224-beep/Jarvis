#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define DAV_HREF 192
#define DAV_NAME 48

typedef struct {
    char href[DAV_HREF];
    char name[DAV_NAME];
} dav_calendar_t;

typedef struct {
    const char *uid;
    const char *title;
    const char *place;
    time_t start;
    time_t end;
    bool all_day;
    time_t stamp;
} dav_event_t;

typedef void (*dav_data_t)(void *context, const char *text, size_t length);
typedef void (*dav_end_t)(void *context);

typedef struct {
    int state;
    char tag[40];
    int tag_length;
    char entity[12];
    int entity_length;
    int cdata_end;
    char pending[64];
    int pending_length;
    dav_data_t data;
    dav_end_t end;
    void *context;
} dav_stream_t;

extern const char DAV_PRINCIPAL_BODY[];
extern const char DAV_HOME_BODY[];
extern const char DAV_CALENDARS_BODY[];

bool dav_find_href(const char *xml, size_t length, const char *element, char *out, size_t capacity);
int dav_parse_calendars(const char *xml, size_t length, dav_calendar_t *out, int capacity);
void dav_stream_begin(dav_stream_t *stream, dav_data_t data, dav_end_t end, void *context);
void dav_stream_feed(dav_stream_t *stream, const char *data, size_t length);
void dav_stream_finish(dav_stream_t *stream);
char *dav_query_body(time_t from, time_t to);
char *dav_build_event(const dav_event_t *event);
void dav_join_url(const char *origin, const char *href, char *out, size_t capacity);
void dav_origin(const char *url, char *out, size_t capacity);
