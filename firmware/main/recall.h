#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

void recall_start(void);
bool recall_enabled(void);
void recall_set_enabled(bool enabled);

void recall_session_begin(void);
void recall_heard(const char *text);
void recall_spoken(const char *text);
void recall_session_end(void);

void recall_tick(bool idle);

char *recall_context(void);

char *recall_tool_search(const char *args_json);
char *recall_tool_remember(const char *args_json);
char *recall_tool_forget(const char *args_json);

cJSON *recall_status(void);
cJSON *recall_lines(const char *query, int limit);
bool recall_set_notes(const char *notes);
bool recall_wipe(void);
