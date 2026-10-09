#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "proto.h"

typedef struct {
    int setup;
    int interrupted;
    int turn;
    int away;
    size_t samples;
    long checksum;
    char heard[256];
    char spoken[256];
    char tool[512];
} seen_t;

static void on_setup(void *ctx) { ((seen_t *) ctx)->setup++; }
static void on_interrupted(void *ctx) { ((seen_t *) ctx)->interrupted++; }
static void on_turn(void *ctx) { ((seen_t *) ctx)->turn++; }
static void on_away(void *ctx) { ((seen_t *) ctx)->away++; }

static void on_audio(void *ctx, const int16_t *pcm, size_t samples)
{
    seen_t *seen = ctx;
    for (size_t i = 0; i < samples; i++) {
        seen->checksum += pcm[i];
    }
    seen->samples += samples;
}

static void on_heard(void *ctx, const char *text) { strcat(((seen_t *) ctx)->heard, text); }
static void on_spoken(void *ctx, const char *text) { strcat(((seen_t *) ctx)->spoken, text); }

static void on_tool(void *ctx, const char *id, const char *name, const char *args)
{
    snprintf(((seen_t *) ctx)->tool, sizeof(((seen_t *) ctx)->tool), "%s|%s|%s", id, name, args);
}

static const proto_handlers_t HANDLERS = {
    .setup_complete = on_setup,
    .audio = on_audio,
    .input_text = on_heard,
    .output_text = on_spoken,
    .interrupted = on_interrupted,
    .turn_complete = on_turn,
    .tool_call = on_tool,
    .go_away = on_away,
};

static void feed(seen_t *seen, const char *json)
{
    char *copy = strdup(json);
    proto_parse(copy, strlen(copy), &HANDLERS, seen);
    free(copy);
}

int main(void)
{
    uint8_t raw[1000];
    for (size_t i = 0; i < sizeof(raw); i++) {
        raw[i] = (uint8_t) (i * 37 + 11);
    }
    for (size_t length = 0; length <= sizeof(raw); length++) {
        char text[1400];
        uint8_t back[1000];
        size_t encoded = proto_base64_encode(raw, length, text);
        assert(encoded == (length + 2) / 3 * 4);
        assert(proto_base64_decode(text, encoded, back, sizeof(back)) == length);
        assert(memcmp(raw, back, length) == 0);
    }
    printf("base64 round trip OK for 0..1000 bytes\n");

    proto_setup_t setup = {
        .model = "gemini-3.8-live",
        .voice = "Charon",
        .instruction = "Jsi Jarvis. Říkej \"ano\" a \\ zpětné lomítko.\nDruhý řádek.",
        .functions_json = "[{\"name\":\"set_devices\",\"description\":\"x\",\"parameters\":{\"type\":\"OBJECT\","
                          "\"properties\":{\"on\":{\"type\":\"BOOLEAN\"}}}}]",
        .google_search = true,
        .silence_ms = 600,
        .prefix_ms = 200,
    };
    char *text = proto_build_setup(&setup);
    cJSON *root = cJSON_Parse(text);
    assert(root);
    cJSON *body = cJSON_GetObjectItem(root, "setup");
    assert(strcmp(cJSON_GetObjectItem(body, "model")->valuestring, "models/gemini-3.8-live") == 0);
    cJSON *generation = cJSON_GetObjectItem(body, "generationConfig");
    assert(strcmp(cJSON_GetArrayItem(cJSON_GetObjectItem(generation, "responseModalities"), 0)->valuestring, "AUDIO") == 0);
    cJSON *voice = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(generation, "speechConfig"), "voiceConfig"), "prebuiltVoiceConfig"), "voiceName");
    assert(strcmp(voice->valuestring, "Charon") == 0);
    cJSON *part = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(body, "systemInstruction"), "parts"), 0);
    assert(strcmp(cJSON_GetObjectItem(part, "text")->valuestring, setup.instruction) == 0);
    cJSON *tools = cJSON_GetObjectItem(body, "tools");
    assert(cJSON_GetArraySize(tools) == 2);
    assert(cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 0), "googleSearch"));
    cJSON *declarations = cJSON_GetObjectItem(cJSON_GetArrayItem(tools, 1), "functionDeclarations");
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(declarations, 0), "name")->valuestring, "set_devices") == 0);
    assert(cJSON_GetObjectItem(body, "inputAudioTranscription"));
    assert(cJSON_GetObjectItem(body, "outputAudioTranscription"));
    cJSON *detection = cJSON_GetObjectItem(cJSON_GetObjectItem(body, "realtimeInputConfig"), "automaticActivityDetection");
    assert(cJSON_GetObjectItem(detection, "silenceDurationMs")->valueint == 600);
    cJSON_Delete(root);
    printf("setup: %s\n", text);
    proto_free(text);

    setup.google_search = false;
    setup.functions_json = NULL;
    text = proto_build_setup(&setup);
    assert(strstr(text, "\"tools\"") == NULL);
    proto_free(text);

    int16_t pcm[1024];
    for (int i = 0; i < 1024; i++) {
        pcm[i] = (int16_t) (i * 31 - 9000);
    }
    char *message = malloc(proto_audio_capacity(1024));
    size_t length = proto_build_audio(message, proto_audio_capacity(1024), pcm, 1024);
    assert(length == strlen(message));
    root = cJSON_Parse(message);
    cJSON *audio = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "realtimeInput"), "audio");
    assert(strcmp(cJSON_GetObjectItem(audio, "mimeType")->valuestring, "audio/pcm;rate=16000") == 0);
    const char *data = cJSON_GetObjectItem(audio, "data")->valuestring;
    int16_t back[1024];
    assert(proto_base64_decode(data, strlen(data), (uint8_t *) back, sizeof(back)) == sizeof(back));
    assert(memcmp(back, pcm, sizeof(pcm)) == 0);
    cJSON_Delete(root);
    assert(proto_build_audio(message, 10, pcm, 1024) == 0);
    free(message);
    printf("audio message round trip OK\n");

    text = proto_build_tool_response("call-1", "set_devices", "{\"result\":\"ok\",\"changed\":2}");
    assert(strcmp(text, "{\"toolResponse\":{\"functionResponses\":[{\"id\":\"call-1\",\"name\":\"set_devices\","
                        "\"response\":{\"result\":\"ok\",\"changed\":2}}]}}") == 0);
    proto_free(text);
    text = proto_build_tool_response("call-2", "x", "hub neodpovídá \"teď\"");
    root = cJSON_Parse(text);
    assert(root);
    cJSON_Delete(root);
    proto_free(text);
    assert(cJSON_Parse(proto_stream_end()));

    seen_t seen = {0};
    feed(&seen, "{\"setupComplete\":{}}");
    assert(seen.setup == 1);

    int16_t reply[5000];
    long expected = 0;
    for (int i = 0; i < 5000; i++) {
        reply[i] = (int16_t) (i * 13 - 20000);
        expected += reply[i];
    }
    char *encoded = malloc(5000 * 2 * 4 / 3 + 8);
    size_t encoded_length = proto_base64_encode((const uint8_t *) reply, sizeof(reply), encoded);
    encoded[encoded_length] = 0;
    char *big = malloc(encoded_length * 2 + 1024);
    sprintf(big,
            "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/pcm;rate=24000\","
            "\"data\":\"%s\"}},{\"inlineData\": {\"data\" : \"%s\", \"mimeType\":\"audio/pcm;rate=24000\"}}]},"
            "\"outputTranscription\":{\"text\":\"Zhasínám \"},\"turnComplete\":true},"
            "\"usageMetadata\":{\"totalTokenCount\":12}}",
            encoded, encoded);
    feed(&seen, big);
    assert(seen.samples == 10000);
    assert(seen.checksum == 2 * expected);
    assert(strcmp(seen.spoken, "Zhasínám ") == 0);
    assert(seen.turn == 1);
    free(big);
    free(encoded);
    printf("two audio parts decoded: %zu samples, transcript and turnComplete seen\n", seen.samples);

    feed(&seen, "{\"serverContent\":{\"inputTranscription\":{\"text\":\"zhasni \"}}}");
    feed(&seen, "{\"serverContent\":{\"inputTranscription\":{\"text\":\"světlo\"}}}");
    assert(strcmp(seen.heard, "zhasni světlo") == 0);
    feed(&seen, "{\"serverContent\":{\"interrupted\":true}}");
    assert(seen.interrupted == 1);
    feed(&seen, "{\"toolCall\":{\"functionCalls\":[{\"id\":\"function-call-77\",\"name\":\"set_devices\","
                "\"args\":{\"ids\":[\"a_1\",\"b_1\"],\"on\":false}}]}}");
    assert(strcmp(seen.tool, "function-call-77|set_devices|{\"ids\":[\"a_1\",\"b_1\"],\"on\":false}") == 0);
    feed(&seen, "{\"toolCall\":{\"functionCalls\":[{\"name\":\"get_devices\"}]}}");
    assert(strcmp(seen.tool, "|get_devices|{}") == 0);
    feed(&seen, "{\"goAway\":{\"timeLeft\":\"10s\"}}");
    assert(seen.away == 1);
    feed(&seen, "not json at all");
    feed(&seen, "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"inlineData\":{\"data\":\"AAAA");
    feed(&seen, "{\"serverContent\":{\"modelTurn\":{\"parts\":[{\"text\":\"myslím\"}]}}}");
    assert(seen.turn == 1 && seen.setup == 1);

    seen_t fixture = {0};
    FILE *file = fopen("server_messages.jsonl", "r");
    assert(file);
    static char line[8192];
    while (fgets(line, sizeof(line), file)) {
        proto_parse(line, strlen(line), &HANDLERS, &fixture);
    }
    fclose(file);
    printf("fixture: %zu samples (sum %ld), heard '%s', spoken '%s', tool %s\n", fixture.samples, fixture.checksum,
           fixture.heard, fixture.spoken, fixture.tool);
    assert(fixture.samples == 600);
    assert(fixture.checksum == FIXTURE_SUM);
    assert(strcmp(fixture.heard, "zhasni světlo") == 0);
    assert(strcmp(fixture.spoken, "Zhasnuto.") == 0);
    assert(strcmp(fixture.tool, "function-call-1|set_devices|{\"ids\":[\"abc_1\"],\"on\":false}") == 0);
    assert(fixture.turn == 1);
    printf("proto OK\n");
    return 0;
}
