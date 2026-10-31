#pragma once

#include <stdbool.h>
#include <stddef.h>

#define RELEASE_VERSION 16
#define RELEASE_PATH 128
#define RELEASE_MD5 33

typedef struct {
    char version[RELEASE_VERSION];
    char path[RELEASE_PATH];
    char md5[RELEASE_MD5];
    size_t size;
} release_t;

int release_compare(const char *first, const char *second);
bool release_parse(const char *json, size_t length, unsigned long app_offset, release_t *out);
