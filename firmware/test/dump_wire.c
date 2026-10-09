#include <stdio.h>
#include <stdlib.h>

#include "proto.h"

int main(void)
{
    proto_setup_t setup = {
        .model = "gemini-3.8-live",
        .voice = "Charon",
        .instruction = "Jsi Jarvis.",
        .functions_json = "[{\"name\":\"set_devices\",\"description\":\"x\",\"parameters\":{\"type\":\"OBJECT\","
                          "\"properties\":{\"on\":{\"type\":\"BOOLEAN\"}}}}]",
        .google_search = true,
        .silence_ms = 600,
        .prefix_ms = 200,
    };
    char *text = proto_build_setup(&setup);
    puts(text);
    proto_free(text);
    const int16_t pcm[2] = {0x0201, 0x0403};
    char audio[256];
    proto_build_audio(audio, sizeof(audio), pcm, 2);
    puts(audio);
    puts(proto_stream_end());
    text = proto_build_tool_response("call-1", "set_devices", "{\"result\":\"ok\"}");
    puts(text);
    proto_free(text);
    return 0;
}
