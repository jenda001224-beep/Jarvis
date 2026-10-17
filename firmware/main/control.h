#pragma once

#include <stdbool.h>

void control_start(void);
void control_power(const char *id, bool on);
void control_brightness(const char *id, int level);
void control_white(const char *id, int kelvin);
void control_hue(const char *id, int hue);
