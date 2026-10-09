#pragma once

#include <stddef.h>
#include <stdint.h>

#define RESAMPLE_TAPS 24

typedef struct {
    float history[RESAMPLE_TAPS];
    int head;
    int phase;
} resampler_t;

void resampler_init(resampler_t *state);
size_t resampler_capacity(size_t input_samples);
size_t resampler_process(resampler_t *state, const int16_t *in, size_t count, int16_t *out);
