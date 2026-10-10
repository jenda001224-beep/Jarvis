#pragma once

#include <stdbool.h>

#include "fx.h"

typedef enum {
    SCENE_IDLE,
    SCENE_LISTENING,
    SCENE_THINKING,
    SCENE_SPEAKING,
    SCENE_ERROR,
} scene_mode_t;

#define SCENE_IDLE_CENTER 222.0f
#define SCENE_VOICE_CENTER 190.0f
#define SCENE_IDLE_HOLE 142.0f

void scene_init(void);
void scene_set_mode(scene_mode_t mode);
void scene_set_hole(bool keep_centre_clear);
void scene_set_levels(float microphone, float speaker);
void scene_set_minute(float fraction);
void scene_step(float seconds, fx_frame_t *frame);
bool scene_settled(void);
