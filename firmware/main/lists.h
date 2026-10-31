#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "alarms.h"
#include "api.h"
#include "countdown.h"
#include "todos.h"

void lists_init(void);
void lists_save(void);
uint32_t lists_revision(void);
int lists_open_todos(void);
bool lists_due_alarm(time_t now, alarm_t *out);
bool lists_next_alarm(time_t now, alarm_t *out, time_t *when);
bool lists_handle(api_call_t *call);
bool lists_set_todo_done(uint32_t id, bool done);
int lists_todos(todo_t *out, int capacity);
int lists_alarms(alarm_t *out, int capacity);
int lists_timers(countdown_t *out, int capacity);
uint32_t lists_add_timer(int seconds, const char *label);
void lists_cancel_timer(uint32_t id);
bool lists_due_timer(countdown_t *out);

char *lists_tool_add_todo(const char *args_json);
char *lists_tool_list_todos(void);
char *lists_tool_complete_todo(const char *args_json);
char *lists_tool_remove_todo(const char *args_json);
char *lists_tool_set_alarm(const char *args_json);
char *lists_tool_list_alarms(void);
char *lists_tool_cancel_alarm(const char *args_json);
char *lists_tool_set_timer(const char *args_json);
char *lists_tool_list_timers(void);
char *lists_tool_cancel_timer(const char *args_json);
