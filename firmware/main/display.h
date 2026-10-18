#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t display_start(uint16_t **framebuffer);
void display_set_orientation(int quarter);
void display_take_stats(int *draws, int *average_microseconds, int *longest_microseconds);
