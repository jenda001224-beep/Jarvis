#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*web_sink_t)(void *context, const char *data, size_t length);

bool web_stream(const char *url, web_sink_t sink, void *context);
char *web_get(const char *url, size_t limit, size_t *length);
