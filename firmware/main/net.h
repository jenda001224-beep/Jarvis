#pragma once

#include <stdbool.h>

void net_start(const char *ssid, const char *password);
bool net_connected(void);
bool net_time_valid(void);
