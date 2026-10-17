#include "live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "audio.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "memory.h"
#include "proto.h"
#include "settings.h"

#define ENDPOINT                                                                                                       \
    "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
#define RECEIVE_LIMIT (2 * 1024 * 1024)
#define SEND_TIMEOUT pdMS_TO_TICKS(3000)
#define OPCODE_CONTINUATION 0x00
#define OPCODE_TEXT 0x01
#define OPCODE_BINARY 0x02
#define OPCODE_CLOSE 0x08
#define SILENCE_MS 600
#define PREFIX_MS 200
#define UPLINK_SAMPLES 2048

static const char *TAG = "live";

static live_callbacks_t callbacks;
static esp_websocket_client_handle_t client;
static SemaphoreHandle_t send_lock;
static char *setup_message;
static char *receive_buffer;
static size_t receive_length;
static size_t receive_capacity;
static volatile bool ready;
static volatile bool was_ready;
static volatile bool speaking;
static volatile bool ended;
static char close_reason[128];

static void on_setup_complete(void *ctx)
{
    ESP_LOGI(TAG, "session ready");
    ready = true;
    was_ready = true;
    app_post(APP_EVENT_SESSION_READY);
}

static void on_audio(void *ctx, const int16_t *pcm, size_t samples)
{
    if (!speaking) {
        speaking = true;
        app_post(APP_EVENT_MODEL_AUDIO);
    }
    audio_play(pcm, samples);
}

static void on_input_text(void *ctx, const char *text)
{
    if (callbacks.heard) {
        callbacks.heard(text);
    }
    app_post(APP_EVENT_HEARD);
}

static void on_output_text(void *ctx, const char *text)
{
    if (callbacks.spoken) {
        callbacks.spoken(text);
    }
}

static void on_interrupted(void *ctx)
{
    audio_flush();
    app_post(APP_EVENT_INTERRUPTED);
}

static void on_turn_complete(void *ctx)
{
    audio_finish();
    speaking = false;
    app_post(APP_EVENT_TURN_COMPLETE);
}

static void on_tool_call(void *ctx, const char *id, const char *name, const char *args_json)
{
    if (callbacks.tool) {
        callbacks.tool(id, name, args_json);
    }
}

static const proto_handlers_t HANDLERS = {
    .setup_complete = on_setup_complete,
    .audio = on_audio,
    .input_text = on_input_text,
    .output_text = on_output_text,
    .interrupted = on_interrupted,
    .turn_complete = on_turn_complete,
    .tool_call = on_tool_call,
};

static bool reserve(size_t needed)
{
    if (needed <= receive_capacity) {
        return true;
    }
    if (needed > RECEIVE_LIMIT) {
        return false;
    }
    size_t capacity = receive_capacity ? receive_capacity : 32768;
    while (capacity < needed) {
        capacity *= 2;
    }
    char *grown = heap_caps_realloc(receive_buffer, capacity, MALLOC_CAP_SPIRAM);
    if (!grown) {
        return false;
    }
    receive_buffer = grown;
    receive_capacity = capacity;
    return true;
}

static void on_data(const esp_websocket_event_data_t *data)
{
    if (data->op_code == OPCODE_CLOSE) {
        if (data->data_len > 2) {
            size_t length = (size_t) data->data_len - 2;
            if (length >= sizeof(close_reason)) {
                length = sizeof(close_reason) - 1;
            }
            memcpy(close_reason, data->data_ptr + 2, length);
            close_reason[length] = 0;
            ESP_LOGW(TAG, "closed by server: %s", close_reason);
        }
        return;
    }
    if (data->op_code != OPCODE_TEXT && data->op_code != OPCODE_BINARY && data->op_code != OPCODE_CONTINUATION) {
        return;
    }
    if (data->data_len <= 0) {
        return;
    }
    if (data->op_code != OPCODE_CONTINUATION && data->payload_offset == 0) {
        receive_length = 0;
    }
    if (!reserve(receive_length + (size_t) data->data_len + 1)) {
        ESP_LOGE(TAG, "message too large, dropped");
        receive_length = 0;
        return;
    }
    memcpy(receive_buffer + receive_length, data->data_ptr, (size_t) data->data_len);
    receive_length += (size_t) data->data_len;
    bool frame_done = data->payload_offset + data->data_len >= data->payload_len;
    if (frame_done && data->fin) {
        receive_buffer[receive_length] = 0;
        proto_parse(receive_buffer, receive_length, &HANDLERS, NULL);
        receive_length = 0;
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *event)
{
    const esp_websocket_event_data_t *data = event;
    esp_websocket_client_handle_t source = arg;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected, sending setup");
        if (setup_message) {
            esp_websocket_client_send_text(source, setup_message, (int) strlen(setup_message), SEND_TIMEOUT);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        on_data(data);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (data) {
            ESP_LOGW(TAG, "connection error: type %d, tls %s, socket errno %d, handshake status %d",
                     (int) data->error_handle.error_type, esp_err_to_name(data->error_handle.esp_tls_last_esp_err),
                     data->error_handle.esp_transport_sock_errno, data->error_handle.esp_ws_handshake_status_code);
        }
        /* fall through */
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        if (source != client || ended) {
            break;
        }
        ended = true;
        ESP_LOGW(TAG, "connection ended (%d)", (int) id);
        ready = false;
        app_post(APP_EVENT_SESSION_CLOSED);
        break;
    default:
        break;
    }
}

void live_init(const live_callbacks_t *handlers)
{
    callbacks = *handlers;
    send_lock = xSemaphoreCreateMutex();
}

esp_err_t live_open(const char *instruction, const char *functions_json, bool google_search)
{
    live_close();
    const settings_t *settings = settings_get();
    const proto_setup_t setup = {
        .model = settings->model,
        .voice = settings->voice,
        .instruction = instruction,
        .functions_json = functions_json,
        .google_search = google_search,
        .silence_ms = SILENCE_MS,
        .prefix_ms = PREFIX_MS,
    };
    setup_message = proto_build_setup(&setup);
    if (!setup_message) {
        return ESP_ERR_NO_MEM;
    }
    static char url[320];
    snprintf(url, sizeof(url), ENDPOINT "?key=%s", settings->api_key);
    const esp_websocket_client_config_t config = {
        .uri = url,
        .buffer_size = 16384,
        .task_stack = 10240,
        .task_prio = 5,
        .disable_auto_reconnect = true,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 15,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    close_reason[0] = 0;
    receive_length = 0;
    ready = false;
    was_ready = false;
    speaking = false;
    ended = false;
    client = esp_websocket_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, on_event, client);
    return esp_websocket_client_start(client);
}

void live_close(void)
{
    ready = false;
    if (client) {
        xSemaphoreTake(send_lock, portMAX_DELAY);
        esp_websocket_client_handle_t closing = client;
        client = NULL;
        xSemaphoreGive(send_lock);
        if (esp_websocket_client_is_connected(closing)) {
            esp_websocket_client_close(closing, pdMS_TO_TICKS(1500));
        }
        esp_websocket_client_destroy(closing);
    }
    if (setup_message) {
        proto_free(setup_message);
        setup_message = NULL;
    }
}

bool live_ready(void)
{
    return ready;
}

bool live_was_ready(void)
{
    return was_ready;
}

const char *live_close_reason(void)
{
    return close_reason;
}

static bool send_text(const char *text, size_t length)
{
    bool sent = false;
    xSemaphoreTake(send_lock, portMAX_DELAY);
    if (client && ready) {
        sent = esp_websocket_client_send_text(client, text, (int) length, SEND_TIMEOUT) >= 0;
    }
    xSemaphoreGive(send_lock);
    return sent;
}

bool live_send_audio(const int16_t *pcm, size_t samples)
{
    static EXTERNAL_RAM char message[(UPLINK_SAMPLES * 2 + 2) / 3 * 4 + 128];
    if (samples > UPLINK_SAMPLES) {
        samples = UPLINK_SAMPLES;
    }
    size_t length = proto_build_audio(message, sizeof(message), pcm, samples);
    return length > 0 && send_text(message, length);
}

void live_send_stream_end(void)
{
    const char *message = proto_stream_end();
    send_text(message, strlen(message));
}

void live_send_tool_response(const char *id, const char *name, const char *result_json)
{
    char *message = proto_build_tool_response(id, name, result_json);
    if (message) {
        send_text(message, strlen(message));
        proto_free(message);
    }
}

void live_send_text(const char *text)
{
    char *message = proto_build_text(text);
    if (message) {
        send_text(message, strlen(message));
        proto_free(message);
    }
}
