#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t step;
    uint32_t phase;
    int32_t previous;
    int32_t history[2];
    bool smooth;
} rate_t;

typedef enum {
    NOISE_WHITE,
    NOISE_PINK,
    NOISE_BROWN,
} noise_kind_t;

typedef struct {
    uint32_t seed;
    float pink[7];
    float brown;
} noise_t;

typedef enum {
    SOUND_UNKNOWN,
    SOUND_MP3,
    SOUND_AAC,
} sound_format_t;

void rate_init(rate_t *rate, uint32_t input_rate, uint32_t output_rate);
size_t rate_capacity(const rate_t *rate, size_t input_samples);
size_t rate_process(rate_t *rate, const int16_t *in, size_t count, int16_t *out, size_t capacity);
void sound_to_mono(int16_t *pcm, size_t frames, int channels);
void noise_init(noise_t *noise, uint32_t seed);
void noise_fill(noise_t *noise, noise_kind_t kind, int16_t *out, size_t count);
sound_format_t sound_sniff(const uint8_t *data, size_t length);
bool rss_first_item(const char *xml, size_t length, char *title, size_t title_capacity, char *url, size_t url_capacity);
int station_match(const char *wanted);
int station_count(void);
const char *station_name(int index);
const char *station_url(int index);
