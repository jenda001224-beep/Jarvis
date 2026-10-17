#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "home.h"

typedef enum {
    UI_STARTING,
    UI_SETUP_NEEDED,
    UI_OFFLINE,
    UI_IDLE,
    UI_LISTENING,
    UI_THINKING,
    UI_SPEAKING,
    UI_ERROR,
} ui_mode_t;

void ui_init(uint16_t *framebuffer);
void ui_set_mode(ui_mode_t mode, const char *message);
void ui_set_notice(const char *text);
void ui_set_status(const char *text);
void ui_clear_transcript(void);
void ui_append_heard(const char *text);
void ui_append_spoken(const char *text);
void ui_set_lamp(bool on);
bool ui_lamp(void);
void ui_home_update(home_data_t *data);
void ui_set_prefs(const home_prefs_t *prefs);
bool ui_take_prefs(home_prefs_t *out);
void ui_touch(int x, int y, int milliseconds);
void ui_render_stats(int *frames_per_second, int *microseconds_per_frame);
