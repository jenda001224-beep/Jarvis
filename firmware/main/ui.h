#pragma once

#include <stdbool.h>
#include <stdint.h>

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

typedef enum {
    UI_HUB_HIDDEN,
    UI_HUB_PAIR_OFFER,
    UI_HUB_PAIRING,
    UI_HUB_READY,
} ui_hub_t;

void ui_init(uint16_t *framebuffer);
void ui_set_mode(ui_mode_t mode, const char *message);
void ui_set_hub(ui_hub_t hub, int devices);
void ui_set_notice(const char *text);
void ui_set_status(const char *text);
void ui_clear_transcript(void);
void ui_append_heard(const char *text);
void ui_append_spoken(const char *text);
void ui_set_lamp(bool on);
bool ui_lamp(void);
void ui_render_stats(int *frames_per_second, int *microseconds_per_frame);
