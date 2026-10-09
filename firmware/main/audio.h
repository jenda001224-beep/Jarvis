#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_SAMPLE_RATE 16000

esp_err_t audio_init(int volume);
bool audio_read_microphone(int16_t *buffer, size_t samples);
void audio_play(const int16_t *pcm, size_t samples);
void audio_flush(void);
bool audio_playing(void);
int audio_output_level(void);
int audio_level_of(const int16_t *pcm, size_t samples);
