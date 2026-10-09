#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "resample.h"

#define INPUT_RATE 24000.0
#define OUTPUT_RATE 16000.0
#define SAMPLES 24000

static double tone_gain(double frequency, size_t chunk)
{
    static int16_t in[SAMPLES];
    static int16_t out[SAMPLES];
    for (int n = 0; n < SAMPLES; n++) {
        in[n] = (int16_t) lrint(10000.0 * sin(2.0 * M_PI * frequency * n / INPUT_RATE));
    }
    resampler_t state;
    resampler_init(&state);
    size_t produced = 0;
    for (size_t offset = 0; offset < SAMPLES; offset += chunk) {
        size_t take = SAMPLES - offset < chunk ? SAMPLES - offset : chunk;
        produced += resampler_process(&state, in + offset, take, out + produced);
    }
    assert(produced == SAMPLES * 2 / 3);
    double energy = 0.0;
    size_t count = 0;
    for (size_t n = 200; n < produced; n++) {
        energy += (double) out[n] * out[n];
        count++;
    }
    return sqrt(2.0 * energy / count) / 10000.0;
}

static double tone_frequency(double frequency)
{
    static int16_t in[SAMPLES];
    static int16_t out[SAMPLES];
    for (int n = 0; n < SAMPLES; n++) {
        in[n] = (int16_t) lrint(10000.0 * sin(2.0 * M_PI * frequency * n / INPUT_RATE));
    }
    resampler_t state;
    resampler_init(&state);
    size_t produced = resampler_process(&state, in, SAMPLES, out);
    int crossings = 0;
    for (size_t n = 201; n < produced; n++) {
        if (out[n - 1] < 0 && out[n] >= 0) {
            crossings++;
        }
    }
    return crossings * OUTPUT_RATE / (produced - 201);
}

int main(void)
{
    const double pass[] = {100, 300, 1000, 3000, 5000, 6000};
    for (size_t i = 0; i < sizeof(pass) / sizeof(pass[0]); i++) {
        double gain = 20.0 * log10(tone_gain(pass[i], SAMPLES));
        printf("pass %5.0f Hz  %+6.2f dB\n", pass[i], gain);
        assert(fabs(gain) < 1.0);
    }
    const double stop[] = {9500, 10000, 11000, 11900};
    for (size_t i = 0; i < sizeof(stop) / sizeof(stop[0]); i++) {
        double gain = 20.0 * log10(tone_gain(stop[i], SAMPLES) + 1e-9);
        printf("stop %5.0f Hz  %+6.2f dB\n", stop[i], gain);
        assert(gain < -35.0);
    }
    double measured = tone_frequency(1000.0);
    printf("1000 Hz tone comes out at %.1f Hz\n", measured);
    assert(fabs(measured - 1000.0) < 5.0);

    static int16_t in[SAMPLES];
    static int16_t whole[SAMPLES];
    static int16_t pieces[SAMPLES];
    srand(7);
    for (int n = 0; n < SAMPLES; n++) {
        in[n] = (int16_t) (rand() % 20000 - 10000);
    }
    resampler_t a;
    resampler_t b;
    resampler_init(&a);
    resampler_init(&b);
    size_t total = resampler_process(&a, in, SAMPLES, whole);
    size_t produced = 0;
    for (size_t offset = 0; offset < SAMPLES;) {
        size_t take = 1 + (size_t) rand() % 700;
        if (take > SAMPLES - offset) {
            take = SAMPLES - offset;
        }
        size_t made = resampler_process(&b, in + offset, take, pieces + produced);
        assert(made <= resampler_capacity(take));
        produced += made;
        offset += take;
    }
    assert(produced == total);
    for (size_t n = 0; n < total; n++) {
        assert(whole[n] == pieces[n]);
    }
    printf("chunked output identical to one-shot output (%zu samples)\n", total);

    int16_t loud[300];
    int16_t clipped[300];
    for (int n = 0; n < 300; n++) {
        loud[n] = n % 2 ? 32767 : -32768;
    }
    resampler_t c;
    resampler_init(&c);
    resampler_process(&c, loud, 300, clipped);
    printf("resample OK\n");
    return 0;
}
