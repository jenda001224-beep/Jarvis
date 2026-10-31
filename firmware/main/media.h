#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"
#include "sound.h"

void media_start(void);
void media_hold(bool hold);
bool media_waiting(void);
bool media_audible(void);
size_t media_title(char *out, size_t capacity);
void media_stop(void);
void media_play_station(int index);
void media_play_noise(noise_kind_t kind, int minutes);
char *media_tool_radio(const char *args_json);
char *media_tool_podcast(const char *args_json);
char *media_tool_noise(const char *args_json);
char *media_tool_stop(void);
cJSON *media_json(void);
