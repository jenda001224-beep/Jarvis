#include "resample.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define UP 2
#define DOWN 3
#define LENGTH (RESAMPLE_TAPS * UP)
#define CUTOFF (7600.0f / 48000.0f)

static float coefficients[UP][RESAMPLE_TAPS];
static bool ready;

static void design(void)
{
    const float pi = 3.14159265358979f;
    const float centre = (LENGTH - 1) / 2.0f;
    float prototype[LENGTH];
    for (int k = 0; k < LENGTH; k++) {
        float x = k - centre;
        float sinc = fabsf(x) < 1e-6f ? 2.0f * CUTOFF : sinf(2.0f * pi * CUTOFF * x) / (pi * x);
        float window = 0.54f - 0.46f * cosf(2.0f * pi * k / (LENGTH - 1));
        prototype[k] = sinc * window;
    }
    for (int p = 0; p < UP; p++) {
        float sum = 0.0f;
        for (int j = 0; j < RESAMPLE_TAPS; j++) {
            coefficients[p][j] = prototype[p + j * UP];
            sum += coefficients[p][j];
        }
        for (int j = 0; j < RESAMPLE_TAPS; j++) {
            coefficients[p][j] /= sum;
        }
    }
    ready = true;
}

void resampler_init(resampler_t *state)
{
    if (!ready) {
        design();
    }
    memset(state, 0, sizeof(*state));
}

size_t resampler_capacity(size_t input_samples)
{
    return input_samples * UP / DOWN + 2;
}

size_t resampler_process(resampler_t *state, const int16_t *in, size_t count, int16_t *out)
{
    size_t produced = 0;
    for (size_t n = 0; n < count; n++) {
        state->head = (state->head + 1) % RESAMPLE_TAPS;
        state->history[state->head] = in[n];
        while (state->phase < UP) {
            const float *taps = coefficients[state->phase];
            float sum = 0.0f;
            int index = state->head;
            for (int j = 0; j < RESAMPLE_TAPS; j++) {
                sum += taps[j] * state->history[index];
                index = index == 0 ? RESAMPLE_TAPS - 1 : index - 1;
            }
            if (sum > 32767.0f) {
                sum = 32767.0f;
            } else if (sum < -32768.0f) {
                sum = -32768.0f;
            }
            out[produced++] = (int16_t) lrintf(sum);
            state->phase += DOWN;
        }
        state->phase -= UP;
    }
    return produced;
}
