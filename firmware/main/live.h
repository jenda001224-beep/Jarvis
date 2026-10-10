#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    void (*heard)(const char *text);
    void (*spoken)(const char *text);
    void (*tool)(const char *id, const char *name, const char *args_json);
} live_callbacks_t;

void live_init(const live_callbacks_t *callbacks);
esp_err_t live_open(const char *instruction, const char *functions_json, bool google_search);
void live_close(void);
bool live_ready(void);
bool live_was_ready(void);
const char *live_close_reason(void);
bool live_send_audio(const int16_t *pcm, size_t samples);
void live_send_stream_end(void);
void live_send_tool_response(const char *id, const char *name, const char *result_json);
void live_send_text(const char *text);
