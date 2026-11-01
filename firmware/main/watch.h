#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "notices.h"

#define WATCH_PRINTER 1
#define WATCH_DEPARTURE 2
#define WATCH_RAIN 4

void watch_start(void);
void watch_set_mask(int mask);
uint32_t watch_notice(const char *source, const char *text, bool speak);
int watch_notices(notice_t *out, int capacity);
int watch_unread(void);
size_t watch_peek_spoken(char *out, size_t capacity);
size_t watch_take_spoken(char *out, size_t capacity);
size_t watch_describe_unread(char *out, size_t capacity);
void watch_mark_read(void);
void watch_mark_one(uint32_t id);
void watch_silence(void);
void watch_set_away(bool away);
bool watch_away(void);
