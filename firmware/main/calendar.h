#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "agenda.h"

void calendar_start(void);
bool calendar_configured(void);
char *calendar_tool_get(const char *args_json);
size_t calendar_today(char *out, size_t capacity);
size_t calendar_next(char *out, size_t capacity);
int calendar_today_list(agenda_event_t *out, int capacity);
void calendar_refresh_soon(void);
int calendar_between(time_t from, time_t to, agenda_event_t *out, int capacity);
