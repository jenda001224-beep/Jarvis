#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"
#include "resample.h"

#define CHUNK_SAMPLES 1024
#define LINE_LIMIT (4 * 1024 * 1024)

static FILE *reply;
static resampler_t resampler;

static void on_setup(void *ctx)
{
    puts("EVENT ready");
}

static void on_audio(void *ctx, const int16_t *pcm, size_t samples)
{
    static int16_t converted[4096];
    size_t offset = 0;
    size_t total = 0;
    while (offset < samples) {
        size_t take = samples - offset > 2048 ? 2048 : samples - offset;
        size_t produced = resampler_process(&resampler, pcm + offset, take, converted);
        fwrite(converted, sizeof(int16_t), produced, reply);
        total += produced;
        offset += take;
    }
    printf("EVENT audio %zu %zu\n", samples, total);
}

static void on_heard(void *ctx, const char *text)
{
    printf("EVENT heard %s\n", text);
}

static void on_spoken(void *ctx, const char *text)
{
    printf("EVENT spoken %s\n", text);
}

static void on_interrupted(void *ctx)
{
    puts("EVENT interrupted");
}

static void on_turn(void *ctx)
{
    puts("EVENT turn");
}

static void on_tool(void *ctx, const char *id, const char *name, const char *args)
{
    printf("EVENT tool %s %s\n", name, args);
    const char *result = strcmp(name, "get_devices") == 0
                             ? "{\"devices\":[{\"id\":\"lamp_1\",\"name\":\"Lampa\",\"room\":\"Obývák\","
                               "\"type\":\"light\",\"on\":true,\"brightness\":80}]}"
                             : "{\"result\":\"ok\",\"changed\":1,\"failed\":0}";
    char *message = proto_build_tool_response(id, name, result);
    printf("SEND %s\n", message);
    proto_free(message);
}

static void on_away(void *ctx)
{
    puts("EVENT go_away");
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

static void send_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("EVENT error cannot open %s\n", path);
        return;
    }
    static int16_t pcm[CHUNK_SAMPLES];
    static char message[CHUNK_SAMPLES * 3 + 256];
    size_t samples;
    while ((samples = fread(pcm, sizeof(int16_t), CHUNK_SAMPLES, file)) > 0) {
        if (proto_build_audio(message, sizeof(message), pcm, samples) > 0) {
            printf("SEND %s\n", message);
        }
    }
    fclose(file);
    puts("EVENT sent");
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: live_host MODEL VOICE INSTRUCTION FUNCTIONS REPLY_PCM [nosearch]\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    resampler_init(&resampler);
    reply = fopen(argv[5], "wb");
    const proto_setup_t setup = {
        .model = argv[1],
        .voice = argv[2],
        .instruction = argv[3],
        .functions_json = argv[4][0] ? argv[4] : NULL,
        .google_search = argc < 7,
        .silence_ms = 600,
        .prefix_ms = 200,
    };
    char *text = proto_build_setup(&setup);
    printf("SEND %s\n", text);
    proto_free(text);

    char *line = malloc(LINE_LIMIT);
    while (fgets(line, LINE_LIMIT, stdin)) {
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = 0;
        }
        if (strncmp(line, "RECV ", 5) == 0) {
            proto_parse(line + 5, length - 5, &HANDLERS, NULL);
        } else if (strncmp(line, "AUDIO ", 6) == 0) {
            send_file(line + 6);
        } else if (strcmp(line, "END") == 0) {
            printf("SEND %s\n", proto_stream_end());
        } else if (strcmp(line, "QUIT") == 0) {
            break;
        }
        fflush(reply);
    }
    fclose(reply);
    return 0;
}
