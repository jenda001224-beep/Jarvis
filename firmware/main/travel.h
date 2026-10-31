#pragma once

#include <stdbool.h>

#define RESERVE_MINUTES 15

bool travel_configured(void);
char *travel_tool(const char *args_json);
bool travel_minutes(const char *destination, int *minutes);
