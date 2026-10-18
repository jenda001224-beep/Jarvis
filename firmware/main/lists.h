#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "alarms.h"
#include "api.h"

void lists_init(void);
void lists_save(void);
uint32_t lists_revision(void);
int lists_open_todos(void);
bool lists_due_alarm(time_t now, alarm_t *out);
bool lists_next_alarm(time_t now, alarm_t *out, time_t *when);
bool lists_handle(api_call_t *call);

char *lists_tool_add_todo(const char *args_json);
char *lists_tool_list_todos(void);
char *lists_tool_complete_todo(const char *args_json);
char *lists_tool_remove_todo(const char *args_json);
char *lists_tool_set_alarm(const char *args_json);
char *lists_tool_list_alarms(void);
char *lists_tool_cancel_alarm(const char *args_json);
