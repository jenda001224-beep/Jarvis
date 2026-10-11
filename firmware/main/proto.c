#include "proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "memory.h"

#define AUDIO_PREFIX "{\"realtimeInput\":{\"audio\":{\"data\":\""
#define AUDIO_SUFFIX "\",\"mimeType\":\"audio/pcm;rate=16000\"}}}"
#define DECODE_CHARS 4096

static const char ENCODE_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

size_t proto_base64_encode(const uint8_t *in, size_t length, char *out)
{
    size_t o = 0;
    size_t i = 0;
    for (; i + 2 < length; i += 3) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | in[i + 2];
        out[o++] = ENCODE_TABLE[(v >> 18) & 63];
        out[o++] = ENCODE_TABLE[(v >> 12) & 63];
        out[o++] = ENCODE_TABLE[(v >> 6) & 63];
        out[o++] = ENCODE_TABLE[v & 63];
    }
    if (i + 1 == length) {
        uint32_t v = (uint32_t) in[i] << 16;
        out[o++] = ENCODE_TABLE[(v >> 18) & 63];
        out[o++] = ENCODE_TABLE[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (i + 2 == length) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8);
        out[o++] = ENCODE_TABLE[(v >> 18) & 63];
        out[o++] = ENCODE_TABLE[(v >> 12) & 63];
        out[o++] = ENCODE_TABLE[(v >> 6) & 63];
        out[o++] = '=';
    }
    return o;
}

size_t proto_base64_decode(const char *in, size_t length, uint8_t *out, size_t capacity)
{
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < length; i++) {
        int v = decode_char(in[i]);
        if (v < 0) {
            if (in[i] == '=') {
                break;
            }
            continue;
        }
        acc = (acc << 6) | (uint32_t) v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= capacity) {
                break;
            }
            out[o++] = (uint8_t) (acc >> bits);
        }
    }
    return o;
}

char *proto_build_setup(const proto_setup_t *setup)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *body = cJSON_AddObjectToObject(root, "setup");
    char model[96];
    snprintf(model, sizeof(model), "models/%s", setup->model);
    cJSON_AddStringToObject(body, "model", model);

    cJSON *generation = cJSON_AddObjectToObject(body, "generationConfig");
    cJSON *modalities = cJSON_AddArrayToObject(generation, "responseModalities");
    cJSON_AddItemToArray(modalities, cJSON_CreateString("AUDIO"));
    cJSON *speech = cJSON_AddObjectToObject(generation, "speechConfig");
    cJSON *voice = cJSON_AddObjectToObject(speech, "voiceConfig");
    cJSON *prebuilt = cJSON_AddObjectToObject(voice, "prebuiltVoiceConfig");
    cJSON_AddStringToObject(prebuilt, "voiceName", setup->voice);

    cJSON *instruction = cJSON_AddObjectToObject(body, "systemInstruction");
    cJSON *parts = cJSON_AddArrayToObject(instruction, "parts");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", setup->instruction);
    cJSON_AddItemToArray(parts, part);

    cJSON *functions = setup->functions_json ? cJSON_Parse(setup->functions_json) : NULL;
    if (setup->google_search || functions) {
        cJSON *tools = cJSON_AddArrayToObject(body, "tools");
        if (setup->google_search) {
            cJSON *search = cJSON_CreateObject();
            cJSON_AddObjectToObject(search, "googleSearch");
            cJSON_AddItemToArray(tools, search);
        }
        if (functions) {
            cJSON *declarations = cJSON_CreateObject();
            cJSON_AddItemToObject(declarations, "functionDeclarations", functions);
            cJSON_AddItemToArray(tools, declarations);
        }
    }

    cJSON_AddObjectToObject(body, "inputAudioTranscription");
    cJSON_AddObjectToObject(body, "outputAudioTranscription");

    cJSON *realtime = cJSON_AddObjectToObject(body, "realtimeInputConfig");
    cJSON *detection = cJSON_AddObjectToObject(realtime, "automaticActivityDetection");
    cJSON_AddStringToObject(detection, "startOfSpeechSensitivity", "START_SENSITIVITY_LOW");
    cJSON_AddNumberToObject(detection, "prefixPaddingMs", setup->prefix_ms);
    cJSON_AddNumberToObject(detection, "silenceDurationMs", setup->silence_ms);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

size_t proto_audio_capacity(size_t samples)
{
    return sizeof(AUDIO_PREFIX) + sizeof(AUDIO_SUFFIX) + ((samples * 2 + 2) / 3) * 4 + 1;
}

size_t proto_build_audio(char *out, size_t capacity, const int16_t *pcm, size_t samples)
{
    if (capacity < proto_audio_capacity(samples)) {
        return 0;
    }
    size_t o = sizeof(AUDIO_PREFIX) - 1;
    memcpy(out, AUDIO_PREFIX, o);
    o += proto_base64_encode((const uint8_t *) pcm, samples * 2, out + o);
    memcpy(out + o, AUDIO_SUFFIX, sizeof(AUDIO_SUFFIX));
    return o + sizeof(AUDIO_SUFFIX) - 1;
}

char *proto_build_tool_response(const char *id, const char *name, const char *result_json)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *body = cJSON_AddObjectToObject(root, "toolResponse");
    cJSON *responses = cJSON_AddArrayToObject(body, "functionResponses");
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "id", id);
    cJSON_AddStringToObject(entry, "name", name);
    cJSON *result = result_json ? cJSON_Parse(result_json) : NULL;
    if (!result || !cJSON_IsObject(result)) {
        cJSON_Delete(result);
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "result", result_json ? result_json : "ok");
    }
    cJSON_AddItemToObject(entry, "response", result);
    cJSON_AddItemToArray(responses, entry);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *proto_build_text(const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *content = cJSON_AddObjectToObject(root, "clientContent");
    cJSON *turns = cJSON_AddArrayToObject(content, "turns");
    cJSON *turn = cJSON_CreateObject();
    cJSON_AddStringToObject(turn, "role", "user");
    cJSON *parts = cJSON_AddArrayToObject(turn, "parts");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", text);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToArray(turns, turn);
    cJSON_AddBoolToObject(content, "turnComplete", true);
    char *message = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return message;
}

void proto_free(char *text)
{
    cJSON_free(text);
}

const char *proto_stream_end(void)
{
    return "{\"realtimeInput\":{\"audioStreamEnd\":true}}";
}

static long find_token(const char *buffer, size_t length, size_t from, const char *token)
{
    size_t token_length = strlen(token);
    if (length < token_length) {
        return -1;
    }
    for (size_t i = from; i + token_length <= length; i++) {
        if (buffer[i] == token[0] && memcmp(buffer + i, token, token_length) == 0) {
            return (long) i;
        }
    }
    return -1;
}

static size_t extract_audio(char *message, size_t length, const proto_handlers_t *handlers, void *ctx)
{
    static EXTERNAL_RAM int16_t decoded[DECODE_CHARS / 4 * 3 / 2];
    size_t position = 0;
    for (;;) {
        long anchor = find_token(message, length, position, "\"inlineData\"");
        if (anchor < 0) {
            break;
        }
        long key = find_token(message, length, (size_t) anchor + 12, "\"data\"");
        if (key < 0) {
            break;
        }
        size_t i = (size_t) key + 6;
        while (i < length && isspace((unsigned char) message[i])) {
            i++;
        }
        if (i >= length || message[i] != ':') {
            position = (size_t) key + 6;
            continue;
        }
        i++;
        while (i < length && isspace((unsigned char) message[i])) {
            i++;
        }
        if (i >= length || message[i] != '"') {
            position = (size_t) key + 6;
            continue;
        }
        i++;
        size_t start = i;
        while (i < length && message[i] != '"') {
            i++;
        }
        if (i >= length) {
            break;
        }
        size_t end = i;
        if (handlers->audio) {
            for (size_t offset = start; offset < end; offset += DECODE_CHARS) {
                size_t take = end - offset;
                if (take > DECODE_CHARS) {
                    take = DECODE_CHARS;
                }
                size_t bytes = proto_base64_decode(message + offset, take, (uint8_t *) decoded, sizeof(decoded));
                if (bytes >= 2) {
                    handlers->audio(ctx, decoded, bytes / 2);
                }
            }
        }
        memmove(message + start, message + end, length - end);
        length -= end - start;
        position = start;
    }
    return length;
}

static const char *string_at(const cJSON *object, const char *first, const char *second)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, first);
    if (second) {
        item = cJSON_GetObjectItemCaseSensitive(item, second);
    }
    return cJSON_IsString(item) && item->valuestring[0] ? item->valuestring : NULL;
}

void proto_parse(char *message, size_t length, const proto_handlers_t *handlers, void *ctx)
{
    length = extract_audio(message, length, handlers, ctx);
    cJSON *root = cJSON_ParseWithLength(message, length);
    if (!root) {
        return;
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "setupComplete") && handlers->setup_complete) {
        handlers->setup_complete(ctx);
    }
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "serverContent");
    if (content) {
        const char *heard = string_at(content, "inputTranscription", "text");
        if (heard && handlers->input_text) {
            handlers->input_text(ctx, heard);
        }
        const char *spoken = string_at(content, "outputTranscription", "text");
        if (spoken && handlers->output_text) {
            handlers->output_text(ctx, spoken);
        }
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(content, "interrupted")) && handlers->interrupted) {
            handlers->interrupted(ctx);
        }
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(content, "turnComplete")) && handlers->turn_complete) {
            handlers->turn_complete(ctx);
        }
    }
    const cJSON *call = cJSON_GetObjectItemCaseSensitive(root, "toolCall");
    const cJSON *function;
    cJSON_ArrayForEach(function, cJSON_GetObjectItemCaseSensitive(call, "functionCalls"))
    {
        const char *name = string_at(function, "name", NULL);
        const char *id = string_at(function, "id", NULL);
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(function, "args");
        if (!name || !handlers->tool_call) {
            continue;
        }
        char *args_json = args ? cJSON_PrintUnformatted(args) : NULL;
        handlers->tool_call(ctx, id ? id : "", name, args_json ? args_json : "{}");
        cJSON_free(args_json);
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "goAway") && handlers->go_away) {
        handlers->go_away(ctx);
    }
    cJSON_Delete(root);
}
