#pragma once

#include <stdbool.h>
#include <stddef.h>

size_t geo_encode(const char *text, char *out, size_t capacity);
bool geo_parse_place(const char *json, size_t length, double *latitude, double *longitude);
bool geo_parse_route(const char *json, size_t length, double *seconds, double *meters);
