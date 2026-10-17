#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_CALENDARS 3

typedef struct {
    char ssid[33];
    char password[65];
    char api_key[96];
    char model[48];
    char voice[24];
    char place[40];
    char home[96];
    char transport[12];
    char printer_code[16];
    char printer_address[40];
    char printer_serial[24];
    char calendars[SETTINGS_CALENDARS][256];
    int volume;
    int sensitivity;
    bool search;
} settings_t;

bool settings_load(void);
const settings_t *settings_get(void);
bool settings_read_hub_token(char *out, size_t capacity);
void settings_write_hub_token(const char *token);
uint32_t settings_read_number(const char *name);
void settings_write_number(const char *name, uint32_t value);
size_t settings_read_reminders(void *out, size_t capacity);
void settings_write_reminders(const void *data, size_t length);
