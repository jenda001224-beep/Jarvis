#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COUNTDOWN_MAX 8
#define COUNTDOWN_LABEL 40
#define COUNTDOWN_LONGEST_SECONDS (24 * 60 * 60)

typedef struct {
    uint32_t id;
    int64_t ends_ms;
    int seconds;
    char label[COUNTDOWN_LABEL];
} countdown_t;

uint32_t countdown_add(int64_t now_ms, int seconds, const char *label);
bool countdown_cancel(uint32_t id);
uint32_t countdown_find(const char *label);
int countdown_clear(void);
int countdown_list(countdown_t *out, int capacity);
bool countdown_due(int64_t now_ms, countdown_t *out);
void countdown_text(int64_t remaining_ms, char *out, size_t capacity);
