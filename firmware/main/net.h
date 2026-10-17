#pragma once

#include <stdbool.h>
#include <stddef.h>

void net_start(const char *ssid, const char *password);
bool net_connected(void);
int net_signal(void);
void net_address(char *out, size_t capacity);
bool net_time_valid(void);
