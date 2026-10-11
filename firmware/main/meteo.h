#pragma once

#include <stdbool.h>
#include <stddef.h>

const char *meteo_sky(int code);
bool meteo_parse_place(const char *json, size_t length, double *latitude, double *longitude, char *name,
                       size_t capacity);
char *meteo_describe(const char *json, size_t length, const char *place);
