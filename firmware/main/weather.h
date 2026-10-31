#pragma once

#include <stdbool.h>
#include <time.h>

char *weather_tool(const char *args_json);
void weather_start(void);
bool weather_current(int *celsius, int *code);
bool weather_rain_at(time_t *when);
bool weather_dark(void);
