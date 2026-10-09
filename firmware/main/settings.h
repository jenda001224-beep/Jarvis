#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char ssid[33];
    char password[65];
    char api_key[96];
    char model[48];
    char voice[24];
    char place[40];
    int volume;
    bool search;
} settings_t;

bool settings_load(void);
const settings_t *settings_get(void);
bool settings_read_hub_token(char *out, size_t capacity);
void settings_write_hub_token(const char *token);
