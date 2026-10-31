#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*web_sink_t)(void *context, const char *data, size_t length);

bool web_stream(const char *url, web_sink_t sink, void *context);
bool web_stream_from(const char *url, size_t offset, web_sink_t sink, void *context);
char *web_get(const char *url, size_t limit, size_t *length);
typedef enum {
    WEB_GET,
    WEB_POST,
    WEB_PUT,
    WEB_DELETE,
    WEB_PROPFIND,
    WEB_REPORT,
} web_method_t;

typedef struct {
    const char *name;
    const char *value;
} web_header_t;

typedef struct {
    const char *url;
    web_method_t method;
    const web_header_t *headers;
    int header_count;
    const char *body;
    size_t limit;
    web_sink_t sink;
    void *context;
    int timeout_ms;
} web_request_t;

char *web_request(const web_request_t *request, int *status, size_t *length);
char *web_post(const char *url, const char *authorization, const char *body, size_t limit, int *status, size_t *length);
