#pragma once

#include <stdbool.h>
#include <stddef.h>

#define CONSOLE_LINE 256

void console_start(void);
size_t console_take(char *out, size_t capacity);
bool console_inject(const char *line);
