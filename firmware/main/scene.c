#include "scene.h"

#include <math.h>

#define STIFFNESS 190.0f
#define DAMPING 24.0f
#define FADE_RATE 7.0f
#define SETTLE_DISTANCE 0.6f
#define SETTLE_SPEED 4.0f
#define SECONDS_RING 2

typedef struct {
    float value;
    float velocity;
    float target;
} spring_t;

typedef struct {
    float center;
    float radius[FX_RINGS];
    float alpha[FX_RINGS];
    float speed[FX_RINGS];
    float orb;
    float edge;
    float swirl;
} pose_t;

static const pose_t POSE[] = {
    [SCENE_IDLE] = {SCENE_IDLE_CENTER, {152, 166, 180, 192}, {0.86f, 0.52f, 0.78f, 0.36f},
                    {0.010f, -0.006f, 0.0f, 0.002f}, 0, 0.0f, 0.05f},
    [SCENE_LISTENING] = {SCENE_VOICE_CENTER, {100, 113, 126, 138}, {1.0f, 0.62f, 0.86f, 0.42f},
                         {0.050f, -0.030f, 0.120f, 0.006f}, 60, 0.75f, 0.16f},
    [SCENE_THINKING] = {SCENE_VOICE_CENTER, {96, 109, 122, 134}, {0.80f, 0.55f, 1.0f, 0.36f},
                        {0.220f, -0.110f, 0.850f, 0.010f}, 50, 0.42f, 0.55f},
    [SCENE_SPEAKING] = {SCENE_VOICE_CENTER, {102, 115, 128, 140}, {1.0f, 0.66f, 0.80f, 0.46f},
                        {0.070f, -0.045f, 0.160f, 0.008f}, 62, 0.80f, 0.24f},
    [SCENE_ERROR] = {SCENE_VOICE_CENTER, {100, 113, 126, 138}, {0.90f, 0.40f, 0.30f, 0.50f},
                     {0.004f, -0.004f, 0.010f, 0.0f}, 44, 0.0f, 0.03f},
};

static volatile scene_mode_t mode = SCENE_IDLE;
static volatile bool hole = true;
static volatile float microphone_level;
static volatile float speaker_level;

static spring_t center;
static spring_t radius[FX_RINGS];
static spring_t orb;
static float alpha[FX_RINGS];
static float speed[FX_RINGS];
static float turn[FX_RINGS];
static float edge;
static float swirl;
static float warmth;
static float orb_turn;
static float edge_turn;
static float level;
static float clock_seconds;
static volatile float minute_fraction = -1.0f;

static void advance(spring_t *spring, float seconds)
{
    float force = STIFFNESS * (spring->target - spring->value) - DAMPING * spring->velocity;
    spring->velocity += force * seconds;
    spring->value += spring->velocity * seconds;
}

static void approach(float *value, float target, float rate, float seconds)
{
    float blend = 1.0f - expf(-rate * seconds);
    *value += (target - *value) * blend;
}

void scene_init(void)
{
    const pose_t *pose = &POSE[SCENE_IDLE];
    center = (spring_t){pose->center, 0.0f, pose->center};
    orb = (spring_t){0.0f, 0.0f, 0.0f};
    for (int i = 0; i < FX_RINGS; i++) {
        radius[i] = (spring_t){pose->radius[i] - 8.0f, 0.0f, pose->radius[i]};
        alpha[i] = 0.0f;
        speed[i] = pose->speed[i];
        turn[i] = 0.17f * i;
    }
    edge = 0.0f;
    swirl = pose->swirl;
    warmth = 0.0f;
    level = 0.0f;
}

void scene_set_mode(scene_mode_t next)
{
    mode = next;
}

void scene_set_hole(bool keep_centre_clear)
{
    hole = keep_centre_clear;
}

void scene_set_minute(float fraction)
{
    minute_fraction = fraction;
}

void scene_set_levels(float microphone, float speaker)
{
    microphone_level = microphone;
    speaker_level = speaker;
}

bool scene_settled(void)
{
    if (fabsf(center.target - center.value) > SETTLE_DISTANCE || fabsf(center.velocity) > SETTLE_SPEED) {
        return false;
    }
    if (fabsf(orb.target - orb.value) > SETTLE_DISTANCE * 2.0f && mode == SCENE_IDLE) {
        return false;
    }
    for (int i = 0; i < FX_RINGS; i++) {
        if (fabsf(radius[i].target - radius[i].value) > SETTLE_DISTANCE || fabsf(radius[i].velocity) > SETTLE_SPEED) {
            return false;
        }
    }
    return true;
}

void scene_step(float seconds, fx_frame_t *frame)
{
    if (seconds > 0.1f) {
        seconds = 0.1f;
    }
    scene_mode_t current = mode;
    const pose_t *pose = &POSE[current];
    clock_seconds += seconds;

    float heard = current == SCENE_SPEAKING ? speaker_level : current == SCENE_LISTENING ? microphone_level : 0.0f;
    approach(&level, heard, heard > level ? 26.0f : 7.0f, seconds);
    float breath = 0.5f + 0.5f * sinf(clock_seconds * (current == SCENE_THINKING ? 5.2f : 1.7f));

    center.target = pose->center;
    float pulse = current == SCENE_THINKING ? 5.0f * breath : current == SCENE_IDLE ? 0.0f : 3.0f * breath;
    orb.target = pose->orb > 0.0f ? pose->orb + level * 20.0f + pulse : 0.0f;
    const int steps = 4;
    for (int s = 0; s < steps; s++) {
        advance(&center, seconds / steps);
        advance(&orb, seconds / steps);
        for (int i = 0; i < FX_RINGS; i++) {
            radius[i].target = pose->radius[i] + (current == SCENE_IDLE ? 0.0f : level * (4.0f + 1.6f * i));
            advance(&radius[i], seconds / steps);
        }
    }
    if (orb.value < 0.0f) {
        orb.value = 0.0f;
    }

    for (int i = 0; i < FX_RINGS; i++) {
        approach(&alpha[i], pose->alpha[i], FADE_RATE, seconds);
        approach(&speed[i], pose->speed[i], 3.0f, seconds);
        turn[i] += speed[i] * seconds;
        if (i == SECONDS_RING && current == SCENE_IDLE && minute_fraction >= 0.0f) {
            float wanted = minute_fraction - 0.25f;
            float gap = wanted - turn[i];
            gap -= floorf(gap + 0.5f);
            turn[i] += gap * (1.0f - expf(-5.0f * seconds)) - speed[i] * seconds;
        }
        turn[i] -= floorf(turn[i]);
        frame->ring[i].radius = radius[i].value;
        frame->ring[i].alpha = alpha[i] * (current == SCENE_IDLE ? 0.94f + 0.06f * breath : 0.86f + 0.14f * level);
        frame->ring[i].turn = turn[i];
    }

    approach(&edge, pose->edge * (0.45f + 0.55f * (current == SCENE_THINKING ? breath : level)), 6.0f, seconds);
    approach(&swirl, pose->swirl + level * 0.35f, 4.0f, seconds);
    approach(&warmth, current == SCENE_ERROR ? 1.0f : 0.0f, 8.0f, seconds);
    orb_turn += swirl * seconds;
    edge_turn += (0.06f + 0.10f * level) * seconds;
    edge_turn -= floorf(edge_turn);

    frame->center_y = center.value;
    frame->hole = hole ? SCENE_IDLE_HOLE : 0.0f;
    frame->orb_radius = orb.value;
    frame->orb_energy = current == SCENE_ERROR ? 0.5f : 0.55f + 0.45f * (current == SCENE_THINKING ? breath : level);
    frame->orb_turn = orb_turn;
    frame->warmth = warmth;
    frame->edge = edge;
    frame->edge_turn = edge_turn;
}
