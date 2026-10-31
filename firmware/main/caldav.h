#pragma once

#include <stdbool.h>
#include <time.h>

#include "agenda.h"
#include "cJSON.h"

bool caldav_configured(void);
bool caldav_read(time_t from, time_t to, agenda_parser_t *parser);
char *caldav_tool_add(const char *args_json);
cJSON *caldav_status(void);
