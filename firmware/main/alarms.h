#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ALARMS_MAX 10
#define ALARM_LABEL 48
#define ALARM_EVERY_DAY 0x7F

typedef struct {
    uint32_t id;
    uint8_t hour;
    uint8_t minute;
    uint8_t days;
    bool enabled;
    int32_t fired_day;
    char label[ALARM_LABEL];
} alarm_t;

void alarms_load(const void *blob, size_t length);
size_t alarms_save(void *blob, size_t capacity);
size_t alarms_blob_size(void);
uint32_t alarms_add(int hour, int minute, uint8_t days, const char *label);
bool alarms_update(uint32_t id, int hour, int minute, uint8_t days, const char *label, bool enabled);
bool alarms_remove(uint32_t id);
bool alarms_get(uint32_t id, alarm_t *out);
int alarms_list(alarm_t *out, int capacity);
bool alarms_due(time_t now, alarm_t *out);
bool alarms_next(time_t now, alarm_t *out, time_t *when);
uint8_t alarms_day_bit(int weekday);
