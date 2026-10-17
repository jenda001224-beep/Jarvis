#pragma once

#include <stdbool.h>

char *weather_tool(const char *args_json);
void weather_start(void);
bool weather_current(int *celsius, int *code);
