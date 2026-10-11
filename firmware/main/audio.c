#include "audio.h"

#include <math.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "resample.h"

#define PLAY_BUFFER_BYTES (1024 * 1024)
#define PLAY_CHUNK_SAMPLES 512
#define INPUT_CHUNK_SAMPLES 768
#define MICROPHONE_GAIN_DB 37.5f
#define POLL_MS 20
#define DRAINED_AFTER_POLLS 10
#define AMPLIFIER_OFF_AFTER_POLLS 100
#define START_BYTES (AUDIO_SAMPLE_RATE * sizeof(int16_t) * 3 / 10)
#define START_WAIT_MS 600

static const char *TAG = "audio";

static esp_codec_dev_handle_t speaker;
static esp_codec_dev_handle_t microphone;
static StreamBufferHandle_t play_buffer;
static StaticStreamBuffer_t play_buffer_state;
static resampler_t resampler;
static volatile bool playing;
static volatile bool flush_requested;
static volatile bool draining;
static volatile bool muted;
static volatile int output_level;
static volatile uint32_t played_samples;
static volatile uint32_t underruns;
static volatile uint32_t longest_gap;
static volatile int64_t last_arrival;

int audio_level_of(const int16_t *pcm, size_t samples)
{
    if (samples == 0) {
        return 0;
    }
    uint64_t energy = 0;
    for (size_t i = 0; i < samples; i++) {
        energy += (int32_t) pcm[i] * pcm[i];
    }
    float rms = sqrtf((float) (energy / samples));
    if (rms < 1.0f) {
        return 0;
    }
    float decibels = 20.0f * log10f(rms / 32768.0f);
    float scaled = (decibels + 55.0f) / 40.0f;
    if (scaled < 0.0f) {
        scaled = 0.0f;
    } else if (scaled > 1.0f) {
        scaled = 1.0f;
    }
    return (int) (scaled * 100.0f);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void play_task(void *arg)
{
    static int16_t chunk[PLAY_CHUNK_SAMPLES];
    uint8_t *bytes = (uint8_t *) chunk;
    size_t held = 0;
    int idle_polls = 0;
    int64_t waiting_since = 0;
    bool running = false;
    bool amplifier = false;

    for (;;) {
        if (flush_requested) {
            while (xStreamBufferReceive(play_buffer, bytes, sizeof(chunk), 0) > 0) {
            }
            flush_requested = false;
            held = 0;
            running = false;
            waiting_since = 0;
            playing = false;
            output_level = 0;
        }
        if (amplifier && muted) {
            bsp_audio_poweramp_enable(false);
            amplifier = false;
        }
        size_t buffered = xStreamBufferBytesAvailable(play_buffer);
        if (!running && buffered >= sizeof(int16_t)) {
            if (!waiting_since) {
                waiting_since = now_ms();
            }
            running = buffered >= START_BYTES || draining || now_ms() - waiting_since >= START_WAIT_MS;
        }
        if (!running) {
            idle_polls++;
            if (buffered < sizeof(int16_t) && idle_polls >= DRAINED_AFTER_POLLS) {
                playing = false;
                output_level = 0;
            }
            if (amplifier && idle_polls >= AMPLIFIER_OFF_AFTER_POLLS) {
                bsp_audio_poweramp_enable(false);
                amplifier = false;
            }
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }
        size_t available = held + xStreamBufferReceive(play_buffer, bytes + held, sizeof(chunk) - held, 0);
        size_t usable = available & ~(size_t) 1;
        if (usable == 0) {
            held = available;
            running = false;
            waiting_since = 0;
            output_level = 0;
            if (!draining) {
                underruns++;
            }
            continue;
        }
        if (!amplifier && !muted) {
            bsp_audio_poweramp_enable(true);
            amplifier = true;
        }
        idle_polls = 0;
        output_level = audio_level_of(chunk, usable / 2);
        esp_codec_dev_write(speaker, chunk, (int) usable);
        played_samples += (uint32_t) (usable / 2);
        held = available - usable;
        if (held) {
            bytes[0] = bytes[usable];
        }
    }
}

esp_err_t audio_init(int volume)
{
    const i2s_std_config_t i2s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = BSP_I2S_MCLK,
                .bclk = BSP_I2S_SCLK,
                .ws = BSP_I2S_LCLK,
                .dout = BSP_I2S_DOUT,
                .din = BSP_I2S_DSIN,
            },
    };
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");
    ESP_RETURN_ON_ERROR(bsp_audio_init(&i2s), TAG, "i2s");
    speaker = bsp_audio_codec_speaker_init();
    microphone = bsp_audio_codec_microphone_init();
    if (!speaker || !microphone) {
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(microphone, &format) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    muted = volume <= 0;
    esp_codec_dev_set_out_vol(speaker, volume);
    esp_codec_dev_set_out_mute(speaker, muted);
    esp_codec_dev_set_in_gain(microphone, MICROPHONE_GAIN_DB);
    bsp_audio_poweramp_enable(false);
    int gain_left = 0;
    int gain_right = 0;
    esp_codec_dev_read_reg(microphone, 0x43, &gain_left);
    esp_codec_dev_read_reg(microphone, 0x44, &gain_right);
    ESP_LOGI(TAG, "microphone gain registers 0x%02x 0x%02x", gain_left, gain_right);

    uint8_t *storage = heap_caps_malloc(PLAY_BUFFER_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (!storage) {
        return ESP_ERR_NO_MEM;
    }
    play_buffer = xStreamBufferCreateStatic(PLAY_BUFFER_BYTES, 2, storage, &play_buffer_state);
    resampler_init(&resampler);
    xTaskCreatePinnedToCoreWithCaps(play_task, "play", 4096, NULL, 6, NULL, 1, MALLOC_CAP_SPIRAM);
    return ESP_OK;
}

bool audio_read_microphone(int16_t *buffer, size_t samples)
{
    return esp_codec_dev_read(microphone, buffer, (int) (samples * sizeof(int16_t))) == ESP_CODEC_DEV_OK;
}

void audio_play(const int16_t *pcm, size_t samples)
{
    static int16_t converted[INPUT_CHUNK_SAMPLES];
    while (samples > 0) {
        size_t take = samples > INPUT_CHUNK_SAMPLES ? INPUT_CHUNK_SAMPLES : samples;
        size_t produced = resampler_process(&resampler, pcm, take, converted);
        if (produced > 0 && !flush_requested) {
            int64_t now = now_ms();
            if (playing && last_arrival && now - last_arrival > longest_gap) {
                longest_gap = (uint32_t) (now - last_arrival);
            }
            last_arrival = now;
            draining = false;
            playing = true;
            xStreamBufferSend(play_buffer, converted, produced * sizeof(int16_t), pdMS_TO_TICKS(4000));
        }
        pcm += take;
        samples -= take;
    }
}

void audio_finish(void)
{
    draining = true;
}

void audio_flush(void)
{
    flush_requested = true;
}

void audio_set_muted(bool value)
{
    muted = value;
    esp_codec_dev_set_out_mute(speaker, value);
}

bool audio_muted(void)
{
    return muted;
}

void audio_take_stats(audio_stats_t *stats)
{
    stats->played_ms = played_samples * 1000 / AUDIO_SAMPLE_RATE;
    stats->underruns = underruns;
    stats->longest_gap_ms = longest_gap;
    played_samples = 0;
    underruns = 0;
    longest_gap = 0;
    last_arrival = 0;
}

bool audio_playing(void)
{
    return playing || flush_requested;
}

int audio_output_level(void)
{
    return output_level;
}
