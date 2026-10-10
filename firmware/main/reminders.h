#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMINDERS_MAX 8
#define REMINDER_TEXT 120
#define REMINDER_ATTEMPTS 4
#define REMINDER_RETRY_SECONDS 90

typedef struct {
    uint32_t id;
    uint8_t attempts;
    int64_t due;
    int64_t next_try;
    char text[REMINDER_TEXT];
} reminder_t;

void reminders_load(const void *blob, size_t length);
size_t reminders_save(void *blob, size_t capacity);
size_t reminders_blob_size(void);
uint32_t reminders_add(int64_t due, const char *text);
bool reminders_cancel(uint32_t id);
bool reminders_due(int64_t now, reminder_t *out);
void reminders_defer(uint32_t id, int64_t until);
void reminders_attempted(uint32_t id, int64_t now);
int reminders_unheard(reminder_t *out, int capacity);
int reminders_drop_unheard(void);
int reminders_pending(reminder_t *out, int capacity);
