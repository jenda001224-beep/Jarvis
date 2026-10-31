#pragma once

#include <stdbool.h>
#include <stddef.h>

const char *meteo_sky(int code);
bool meteo_parse_place(const char *json, size_t length, double *latitude, double *longitude, char *name,
                       size_t capacity);
char *meteo_describe(const char *json, size_t length, const char *place);
bool meteo_current(const char *json, size_t length, int *celsius, int *code);
int meteo_rain_in(const char *json, size_t length);

typedef struct {
    int year;
    int month;
    int day;
    int code;
    int high;
    int low;
    int rain;
} meteo_day_t;

int meteo_days(const char *json, size_t length, meteo_day_t *out, int capacity);
bool meteo_sun(const char *json, size_t length, int *sunrise_minutes, int *sunset_minutes);
