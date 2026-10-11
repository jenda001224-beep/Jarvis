#include "wake.h"

#include <string.h>

#include "app.h"
#include "audio.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "model_path.h"
#include "settings.h"

#define CAPTURE_SECONDS 4
#define CAPTURE_BYTES (AUDIO_SAMPLE_RATE * sizeof(int16_t) * CAPTURE_SECONDS)

static const char *TAG = "wake";
static const float THRESHOLDS[] = {0.63f, 0.48f, 0.44f};

static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *afe_data;
static StreamBufferHandle_t capture_buffer;
static StaticStreamBuffer_t capture_buffer_state;
static volatile bool armed;
static volatile bool capturing;
static volatile bool clear_requested;
static volatile int level;
static volatile int peak;
static volatile uint32_t frames;

static void feed_task(void *arg)
{
    int chunk = afe->get_feed_chunksize(afe_data);
    int channels = afe->get_feed_channel_num(afe_data);
    int16_t *buffer = heap_caps_malloc(chunk * channels * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    for (;;) {
        if (!audio_read_microphone(buffer, chunk * channels)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        afe->feed(afe_data, buffer);
    }
}

static void fetch_task(void *arg)
{
    for (;;) {
        afe_fetch_result_t *result = afe->fetch(afe_data);
        if (!result || result->ret_value == ESP_FAIL || result->data_size <= 0) {
            continue;
        }
        if (clear_requested) {
            xStreamBufferReset(capture_buffer);
            clear_requested = false;
        }
        if (result->wakeup_state == WAKENET_DETECTED && armed) {
            ESP_LOGI(TAG, "wake word");
            armed = false;
            capturing = true;
            app_post(APP_EVENT_WAKE);
            continue;
        }
        size_t samples = result->data_size / sizeof(int16_t);
        level = audio_level_of(result->data, samples);
        int loudest = 0;
        for (size_t i = 0; i < samples; i++) {
            int magnitude = result->data[i] < 0 ? -result->data[i] : result->data[i];
            if (magnitude > loudest) {
                loudest = magnitude;
            }
        }
        if (loudest > peak) {
            peak = loudest;
        }
        frames++;
        if (capturing) {
            xStreamBufferSend(capture_buffer, result->data, result->data_size, 0);
        }
    }
}

esp_err_t wake_start(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num == 0) {
        ESP_LOGE(TAG, "no wake word model in the model partition");
        return ESP_FAIL;
    }
    afe_config_t *config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!config) {
        return ESP_FAIL;
    }
    config->aec_init = false;
    config->se_init = false;
    config->ns_init = false;
    config->vad_init = false;
    config->wakenet_init = true;
    config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    config->afe_perferred_core = 1;
    ESP_LOGI(TAG, "afe agc %d mode %d, linear gain %.2f, ringbuf %d", (int) config->agc_init, (int) config->agc_mode,
             (double) config->afe_linear_gain, config->afe_ringbuf_size);
    afe = esp_afe_handle_from_config(config);
    afe_data = afe ? afe->create_from_config(config) : NULL;
    afe_config_free(config);
    if (!afe_data) {
        return ESP_FAIL;
    }
    afe->set_wakenet_threshold(afe_data, 1, THRESHOLDS[settings_get()->sensitivity]);

    uint8_t *storage = heap_caps_malloc(CAPTURE_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (!storage) {
        return ESP_ERR_NO_MEM;
    }
    capture_buffer = xStreamBufferCreateStatic(CAPTURE_BYTES, 2, storage, &capture_buffer_state);
    armed = true;
    xTaskCreatePinnedToCoreWithCaps(feed_task, "afe_feed", 8192, NULL, 7, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(fetch_task, "afe_fetch", 8192, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
    return ESP_OK;
}

void wake_set_armed(bool value)
{
    if (!afe_data) {
        return;
    }
    if (value) {
        afe->enable_wakenet(afe_data);
    } else {
        afe->disable_wakenet(afe_data);
    }
    armed = value;
}

void wake_set_capture(bool capture)
{
    capturing = capture;
}

void wake_clear_capture(void)
{
    clear_requested = true;
}

size_t wake_read_capture(int16_t *out, size_t samples, TickType_t wait)
{
    if (!capture_buffer || clear_requested) {
        vTaskDelay(wait);
        return 0;
    }
    size_t wanted = samples * sizeof(int16_t);
    if (xStreamBufferBytesAvailable(capture_buffer) < wanted) {
        vTaskDelay(wait);
        if (xStreamBufferBytesAvailable(capture_buffer) < sizeof(int16_t) * 2) {
            return 0;
        }
    }
    size_t available = xStreamBufferBytesAvailable(capture_buffer) & ~(size_t) 1;
    if (available > wanted) {
        available = wanted;
    }
    return xStreamBufferReceive(capture_buffer, out, available, 0) / sizeof(int16_t);
}

int wake_microphone_level(void)
{
    return level;
}

int wake_take_peak(void)
{
    int value = peak;
    peak = 0;
    return value;
}

uint32_t wake_frame_count(void)
{
    return frames;
}
