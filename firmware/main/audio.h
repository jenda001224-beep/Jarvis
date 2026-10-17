#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_SAMPLE_RATE 16000

typedef struct {
    uint32_t played_ms;
    uint32_t underruns;
    uint32_t longest_gap_ms;
} audio_stats_t;

esp_err_t audio_init(int volume);
bool audio_read_microphone(int16_t *buffer, size_t samples);
void audio_play(const int16_t *pcm, size_t samples);
void audio_finish(void);
void audio_flush(void);
void audio_set_muted(bool muted);
void audio_set_volume(int volume);
bool audio_muted(void);
void audio_take_stats(audio_stats_t *stats);
bool audio_playing(void);
int audio_output_level(void);
int audio_level_of(const int16_t *pcm, size_t samples);
