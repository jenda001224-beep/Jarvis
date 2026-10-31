#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FX_WIDTH 480
#define FX_HEIGHT 480
#define FX_RINGS 4
#define FX_COUNTDOWN_RING 0

typedef struct {
    float radius;
    float alpha;
    float turn;
} fx_ring_t;

typedef struct {
    float center_y;
    float hole;
    float orb_radius;
    float orb_energy;
    float orb_turn;
    float warmth;
    float edge;
    float edge_turn;
    fx_ring_t ring[FX_RINGS];
} fx_frame_t;

bool fx_init(void *(*allocate)(size_t size));
void fx_render(const fx_frame_t *frame, uint16_t *framebuffer);
void fx_invalidate(void);
void fx_clear(uint16_t *framebuffer);
void fx_set_countdown(float fraction);
void fx_set_orientation(int quarter);
void fx_render_orb(uint16_t *framebuffer, int center_y, float radius, float energy, float turn);
