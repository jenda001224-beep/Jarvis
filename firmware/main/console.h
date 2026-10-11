#pragma once

#include <stddef.h>

#define CONSOLE_LINE 256

void console_start(void);
size_t console_take(char *out, size_t capacity);
