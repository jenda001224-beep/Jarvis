#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t wake_start(void);
void wake_set_armed(bool armed);
void wake_set_capture(bool capture);
void wake_clear_capture(void);
size_t wake_read_capture(int16_t *out, size_t samples, TickType_t wait);
int wake_microphone_level(void);
int wake_take_peak(void);
uint32_t wake_frame_count(void);
