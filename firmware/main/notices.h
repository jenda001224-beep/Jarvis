#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTICES_MAX 16
#define NOTICE_TEXT 200
#define NOTICE_SOURCE 16

typedef struct {
    uint32_t id;
    int64_t when;
    bool speak;
    bool read;
    char source[NOTICE_SOURCE];
    char text[NOTICE_TEXT];
} notice_t;

uint32_t notices_add(int64_t when, const char *source, const char *text, bool speak);
int notices_list(notice_t *out, int capacity);
int notices_unread(void);
size_t notices_join(char *out, size_t capacity, bool only_spoken, bool mark);
void notices_mark_all(void);
void notices_clear(void);
