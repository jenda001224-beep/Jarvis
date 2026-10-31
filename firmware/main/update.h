#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

typedef enum {
    UPDATE_IDLE,
    UPDATE_DOWNLOADING,
    UPDATE_DONE,
    UPDATE_FAILED,
} update_phase_t;

void update_start(void);
void update_check_soon(void);
bool update_available(void);
bool update_begin(const char *url, size_t size);
update_phase_t update_phase(void);
int update_percent(void);
void update_clear_failure(void);
void update_tick(bool online);
char *update_tool(const char *args_json);
bool update_take_request(void);
cJSON *update_json(void);
