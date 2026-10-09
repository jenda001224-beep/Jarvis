#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *model;
    const char *voice;
    const char *instruction;
    const char *functions_json;
    bool google_search;
    int silence_ms;
    int prefix_ms;
} proto_setup_t;

typedef struct {
    void (*setup_complete)(void *ctx);
    void (*audio)(void *ctx, const int16_t *pcm, size_t samples);
    void (*input_text)(void *ctx, const char *text);
    void (*output_text)(void *ctx, const char *text);
    void (*interrupted)(void *ctx);
    void (*turn_complete)(void *ctx);
    void (*tool_call)(void *ctx, const char *id, const char *name, const char *args_json);
    void (*go_away)(void *ctx);
} proto_handlers_t;

char *proto_build_setup(const proto_setup_t *setup);
size_t proto_audio_capacity(size_t samples);
size_t proto_build_audio(char *out, size_t capacity, const int16_t *pcm, size_t samples);
char *proto_build_tool_response(const char *id, const char *name, const char *result_json);
void proto_free(char *text);
const char *proto_stream_end(void);
void proto_parse(char *message, size_t length, const proto_handlers_t *handlers, void *ctx);

size_t proto_base64_encode(const uint8_t *in, size_t length, char *out);
size_t proto_base64_decode(const char *in, size_t length, uint8_t *out, size_t capacity);
