#pragma once

#include <stddef.h>
#include <stdint.h>

void logbuf_start(void);
size_t logbuf_read(uint32_t after, char *out, size_t capacity, uint32_t *next);
