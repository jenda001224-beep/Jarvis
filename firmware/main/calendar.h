#pragma once

#include <stdbool.h>
#include <stddef.h>

void calendar_start(void);
bool calendar_configured(void);
char *calendar_tool_get(const char *args_json);
size_t calendar_today(char *out, size_t capacity);
size_t calendar_next(char *out, size_t capacity);
