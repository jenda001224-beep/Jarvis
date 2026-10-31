#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLOUD_RECORD_NAME 40

typedef struct {
    char name[CLOUD_RECORD_NAME];
    int64_t seq;
    const char *data;
} cloud_record_t;

char *cloud_lookup_body(const char *const *names, int count);
char *cloud_modify_body(const char *operation, const char *name, const char *type, int64_t seq, const char *data, const char *text);
void cloud_signature_message(const char *date, const char *body_hash, const char *subpath, char *out, size_t capacity);
