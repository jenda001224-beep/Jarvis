#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"

typedef char *(*chat_tool_t)(const char *name, const char *args_json);

void chat_start(chat_tool_t tool);
bool chat_send(const char *text, char *system, char *functions);
cJSON *chat_json(void);
void chat_clear(void);
uint32_t chat_revision(void);
