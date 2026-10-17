#include "fx.h"

#include "memory.h"

#include <math.h>
#include <string.h>

#define HALF 240
#define QUARTER 4
#define RADIAL_ENTRIES (HALF * QUARTER + 4)
#define ANGLES 4096
#define ANGLE_MASK (ANGLES - 1)
#define QUADRANT 1024
#define ORB_BAND (FX_RINGS + 1)
#define ORB_STEPS 512
#define ORB_UNIT 256
#define ORB_REACH 1.55f
#define EDGE_WIDTH 34
#define EDGE_LIT 28
#define RING_REACH 7.0f
#define MARGIN 1.5f
#define MAX_SPANS (2 * FX_RINGS + 2)
#define GLOW_FALL 2.4f
#ifndef TURN_STEP
#define TURN_STEP 0.5f
#define RADIUS_STEP 0.2f
#define ALPHA_STEP 0.012f
#define ENERGY_STEP 0.02f
#define ALPHA_FLOOR 8
#endif
#define EDGE_STEPS 256
#define PI 3.14159265358979f

typedef struct {
    uint16_t radius;
    uint16_t angle;
} polar_t;

typedef struct {
    uint8_t band;
    uint8_t alpha;
} radial_t;

typedef struct {
    float half_width;
    float glow;
    uint8_t color[3];
} ring_style_t;

typedef struct {
    bool visible;
    float radius;
    float alpha;
    float turn;
} ring_state_t;

typedef struct {
    float low;
    float high;
} span_t;

static const ring_style_t STYLE[FX_RINGS] = {
    {1.5f, 0.34f, {120, 228, 255}},
    {3.0f, 0.10f, {76, 160, 214}},
    {1.2f, 0.30f, {96, 206, 255}},
    {0.7f, 0.16f, {64, 132, 190}},
};

static const uint8_t WARM[3] = {255, 92, 74};

static polar_t *map;
static uint8_t *pattern[FX_RINGS];
static uint8_t sine[256];
static uint8_t orb_body[ORB_STEPS];
static uint8_t orb_core[ORB_STEPS];
static uint8_t orb_halo[ORB_STEPS];
static uint8_t orb_lobe[ORB_STEPS];
static uint16_t orb_lit[ORB_STEPS];
static uint16_t orb_glow[ORB_STEPS];
static uint16_t orb_add[ORB_STEPS][3];
static uint8_t edge_fall[EDGE_WIDTH];
static uint8_t edge_palette[256][3];
static EXTERNAL_RAM uint16_t edge_color[EDGE_STEPS][EDGE_WIDTH];
static uint8_t top_step[FX_WIDTH];
static uint8_t bottom_step[FX_WIDTH];
static uint16_t ramp[FX_RINGS][256];
static radial_t radial[RADIAL_ENTRIES];
static float ramp_warmth = -1.0f;
static ring_state_t shown[FX_RINGS];
static uint8_t dark[RADIAL_ENTRIES];
static float shown_extent;
static float shown_reach;
static float shown_orb;
static float shown_energy;
static float shown_hole;
static int shown_center_x;
static int shown_center_y;
static int orientation;
static bool shown_valid;
static int edge_strength;
static int edge_shift;

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float smooth(float edge0, float edge1, float x)
{
    float t = clampf((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static uint16_t pack(int r, int g, int b)
{
    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }
    return (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static float arc(float degrees, float start, float length, float soft)
{
    float offset = fmodf(degrees - start + 720.0f, 360.0f);
    return smooth(0.0f, soft, offset) * (1.0f - smooth(length - soft, length, offset));
}

static void build_patterns(void)
{
    for (int i = 0; i < ANGLES; i++) {
        float degrees = i * 360.0f / ANGLES;

        float segments = 0.0f;
        for (int k = 0; k < 3; k++) {
            segments += arc(degrees, k * 120.0f, 86.0f, 2.0f);
        }
        pattern[0][i] = (uint8_t) (255.0f * clampf(segments, 0.0f, 1.0f));

        float within = fmodf(degrees, 5.0f);
        int index = (int) (degrees / 5.0f);
        float tick = 1.0f - smooth(0.35f, 0.75f, fabsf(within - 2.5f));
        pattern[1][i] = (uint8_t) (tick * (index % 6 == 0 ? 255.0f : 120.0f));

        float comet = degrees < 130.0f ? powf(1.0f - degrees / 130.0f, 2.2f) : 0.0f;
        comet *= smooth(0.0f, 1.2f, degrees);
        pattern[2][i] = (uint8_t) (255.0f * clampf(0.10f + comet, 0.0f, 1.0f));

        float notch = 1.0f;
        for (int k = 0; k < 4; k++) {
            notch -= arc(degrees, k * 90.0f + 43.0f, 4.0f, 0.8f);
        }
        pattern[3][i] = (uint8_t) (255.0f * clampf(notch, 0.0f, 1.0f));
    }
}

static void build_orb(void)
{
    for (int i = 0; i < 256; i++) {
        sine[i] = (uint8_t) (127.5f + 127.5f * sinf(i * 2.0f * PI / 256.0f));
    }
    for (int i = 0; i < ORB_STEPS; i++) {
        float n = (float) i / ORB_UNIT;
        float inside = 1.0f - smooth(0.965f, 1.0f, n);
        float shade = 0.42f + 0.58f * sqrtf(clampf(1.0f - n * n, 0.0f, 1.0f));
        orb_body[i] = (uint8_t) (255.0f * inside * shade);
        orb_core[i] = (uint8_t) (255.0f * powf(clampf(1.0f - n / 0.62f, 0.0f, 1.0f), 2.4f));
        float rim = expf(-(n - 0.93f) * (n - 0.93f) / 0.0035f) * 0.40f * inside;
        float halo = 0.66f * smooth(0.90f, 1.0f, n) * expf(-(n > 1.0f ? n - 1.0f : 0.0f) / 0.17f);
        orb_halo[i] = (uint8_t) (255.0f * clampf(rim + halo, 0.0f, 1.0f));
        orb_lobe[i] = (uint8_t) (255.0f * smooth(0.04f, 0.42f, n));
    }
    for (int i = 0; i < EDGE_WIDTH; i++) {
        float n = (float) i / EDGE_WIDTH;
        edge_fall[i] = (uint8_t) (255.0f * powf(1.0f - n, 1.7f));
    }
    static const float STOPS[][3] = {
        {40, 200, 255}, {60, 110, 255}, {150, 80, 255}, {255, 70, 190}, {255, 140, 70}, {90, 220, 255}, {40, 200, 255},
    };
    const int count = sizeof(STOPS) / sizeof(STOPS[0]) - 1;
    for (int i = 0; i < 256; i++) {
        float position = i * (float) count / 256.0f;
        int k = (int) position;
        float t = position - k;
        t = t * t * (3.0f - 2.0f * t);
        for (int c = 0; c < 3; c++) {
            edge_palette[i][c] = (uint8_t) (STOPS[k][c] + (STOPS[k + 1][c] - STOPS[k][c]) * t);
        }
    }
}

static void build_ramps(float warmth)
{
    for (int b = 0; b < FX_RINGS; b++) {
        float color[3];
        for (int c = 0; c < 3; c++) {
            color[c] = STYLE[b].color[c] + (WARM[c] - STYLE[b].color[c]) * warmth;
        }
        for (int a = 0; a < 256; a++) {
            float level = a / 255.0f;
            float white = smooth(0.72f, 1.0f, level) * 0.55f;
            ramp[b][a] = pack((int) ((color[0] + (255.0f - color[0]) * white) * level),
                              (int) ((color[1] + (255.0f - color[1]) * white) * level),
                              (int) ((color[2] + (255.0f - color[2]) * white) * level));
        }
    }
    ramp_warmth = warmth;
}

static inline int edge_step(int along)
{
    return (along * 4369) >> 15;
}

static void build_edge_steps(void)
{
    for (int x = 0; x < FX_WIDTH; x++) {
        top_step[x] = (uint8_t) edge_step(x);
        bottom_step[x] = (uint8_t) edge_step(2 * FX_WIDTH + (FX_WIDTH - 1 - x));
    }
}

bool fx_init(void *(*allocate)(size_t size))
{
    map = allocate(sizeof(polar_t) * HALF * HALF);
    for (int b = 0; b < FX_RINGS; b++) {
        pattern[b] = allocate(ANGLES);
        if (!pattern[b]) {
            return false;
        }
    }
    if (!map) {
        return false;
    }
    for (int y = 0; y < HALF; y++) {
        for (int x = 0; x < HALF; x++) {
            float dx = x + 0.5f;
            float dy = y + 0.5f;
            float distance = sqrtf(dx * dx + dy * dy) * QUARTER;
            polar_t *entry = &map[y * HALF + x];
            entry->radius = (uint16_t) (distance >= RADIAL_ENTRIES - 1 ? RADIAL_ENTRIES - 1 : distance + 0.5f);
            entry->angle = (uint16_t) (atan2f(dy, dx) * (QUADRANT / (PI / 2.0f)));
        }
    }
    build_patterns();
    build_orb();
    build_ramps(0.0f);
    build_edge_steps();
    shown_valid = false;
    return true;
}

static float build_radial(float orb_reach)
{
    memset(radial, 0, sizeof(radial));
    float extent = 0.0f;
    if (orb_reach > 0.0f) {
        int last = (int) (orb_reach * QUARTER);
        for (int i = 0; i <= last && i < RADIAL_ENTRIES; i++) {
            radial[i].band = ORB_BAND;
            radial[i].alpha = 255;
        }
        extent = orb_reach;
    }
    for (int b = 0; b < FX_RINGS; b++) {
        const ring_state_t *ring = &shown[b];
        if (!ring->visible) {
            continue;
        }
        const ring_style_t *style = &STYLE[b];
        float reach = style->half_width + RING_REACH;
        int first = (int) ((ring->radius - reach) * QUARTER);
        int last = (int) ((ring->radius + reach) * QUARTER);
        for (int i = first < 0 ? 0 : first; i <= last && i < RADIAL_ENTRIES; i++) {
            float distance = fabsf((float) i / QUARTER - ring->radius);
            float core = 1.0f - smooth(style->half_width - 0.5f, style->half_width + 0.5f, distance);
            float glow = style->glow * expf(-(distance - style->half_width) / GLOW_FALL);
            float value = clampf(core > glow ? core : glow, 0.0f, 1.0f) * ring->alpha;
            uint8_t alpha = (uint8_t) (255.0f * value);
            if (alpha >= ALPHA_FLOOR && (radial[i].band != ORB_BAND || core > 0.2f) && alpha >= radial[i].alpha / 2) {
                if (radial[i].band == 0 || radial[i].band == ORB_BAND || alpha > radial[i].alpha) {
                    radial[i].band = (uint8_t) (b + 1);
                    radial[i].alpha = alpha;
                }
            }
        }
        if (ring->radius + reach > extent) {
            extent = ring->radius + reach;
        }
    }
    return extent;
}

static void add_span(span_t *spans, int *count, float low, float high)
{
    low = low < 0.0f ? 0.0f : low;
    high = high > HALF - 1 ? HALF - 1 : high;
    if (high <= low || *count >= MAX_SPANS) {
        return;
    }
    int at = *count;
    while (at > 0 && spans[at - 1].low > low) {
        spans[at] = spans[at - 1];
        at--;
    }
    spans[at].low = low;
    spans[at].high = high;
    (*count)++;
}

static int merge_spans(span_t *spans, int count)
{
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (kept > 0 && spans[i].low <= spans[kept - 1].high) {
            if (spans[i].high > spans[kept - 1].high) {
                spans[kept - 1].high = spans[i].high;
            }
        } else {
            spans[kept++] = spans[i];
        }
    }
    return kept;
}

static float ring_reach(int b, float alpha)
{
    float brightest = STYLE[b].glow * alpha * 255.0f;
    float tail = brightest > ALPHA_FLOOR ? GLOW_FALL * logf(brightest / ALPHA_FLOOR) : 0.0f;
    tail = tail < 0.5f ? 0.5f : tail > RING_REACH ? RING_REACH : tail;
    return STYLE[b].half_width + tail + MARGIN;
}

static bool update_ring(int b, const fx_ring_t *ring, bool everything, span_t *spans, int *count)
{
    ring_state_t next = {
        .visible = ring->alpha > 0.004f && ring->radius >= 4.0f,
        .radius = ring->radius,
        .alpha = clampf(ring->alpha, 0.0f, 1.0f),
        .turn = ring->turn - floorf(ring->turn),
    };
    ring_state_t *now = &shown[b];
    float drift = fabsf(next.turn - now->turn);
    drift = drift > 0.5f ? 1.0f - drift : drift;
    bool changed = next.visible != now->visible ||
                   (next.visible && (fabsf(next.radius - now->radius) >= RADIUS_STEP ||
                                     fabsf(next.alpha - now->alpha) >= ALPHA_STEP ||
                                     drift * 2.0f * PI * next.radius >= TURN_STEP));
    if (!everything && !changed) {
        return false;
    }
    if (now->visible) {
        float reach = ring_reach(b, now->alpha);
        add_span(spans, count, now->radius - reach, now->radius + reach);
    }
    if (next.visible) {
        float reach = ring_reach(b, next.alpha);
        add_span(spans, count, next.radius - reach, next.radius + reach);
    }
    *now = next;
    return true;
}

typedef struct {
    int scale;
    int energy;
    int phase[3];
    int warm;
} orb_t;

static void prepare_orb(const orb_t *orb)
{
    int red = orb->warm ? 255 : 70;
    int green = orb->warm ? 90 : 190;
    int blue = orb->warm ? 70 : 255;
    for (int index = 0; index < ORB_STEPS; index++) {
        int core = (orb_core[index] * orb->energy) >> 8;
        int halo = (orb_halo[index] * orb->energy) >> 8;
        orb_lit[index] = (uint16_t) ((orb_body[index] * (140 + (orb->energy >> 1))) >> 8);
        orb_add[index][0] = (uint16_t) (core + ((halo * red) >> 8));
        orb_add[index][1] = (uint16_t) (core + ((halo * green) >> 8));
        orb_add[index][2] = (uint16_t) (core + ((halo * blue) >> 8));
        orb_glow[index] = pack(orb_add[index][0], orb_add[index][1], orb_add[index][2]);
    }
}

static inline uint16_t shade_orb(const orb_t *orb, int index, int angle)
{
    int theta = angle >> 4;
    int twist = index >> 1;
    int lobe = orb_lobe[index];
    int a = 128 + (((sine[(theta + orb->phase[0] + twist) & 255] - 128) * lobe) >> 8);
    int b = 128 + (((sine[(theta * 2 + orb->phase[1] - twist) & 255] - 128) * lobe) >> 8);
    int c = 96 + (((sine[(theta * 3 + orb->phase[2] + (twist >> 1)) & 255] - 96) * lobe) >> 8);

    int r = 8 + ((a * 30 + b * 150 + c * 230) >> 9);
    int g = 60 + ((a * 300 + b * 70 + c * 40) >> 9);
    int bl = 190 + ((a * 120 + b * 110 + c * 60) >> 9);
    if (orb->warm) {
        r = 200 + (a >> 2);
        g = 50 + (b >> 3);
        bl = 40 + (c >> 3);
    }
    int lit = orb_lit[index];
    const uint16_t *add = orb_add[index];
    return pack(((r * lit) >> 8) + add[0], ((g * lit) >> 8) + add[1], ((bl * lit) >> 8) + add[2]);
}

static inline uint16_t edge_corner(int x, int y, int depth)
{
    int along = x >= HALF ? FX_WIDTH + y : 3 * FX_WIDTH + (FX_HEIGHT - 1 - y);
    return edge_color[edge_step(along)][depth];
}

static void render_edge(const fx_frame_t *frame, uint16_t *framebuffer)
{
    int strength = (int) (256.0f * clampf(frame->edge, 0.0f, 1.0f));
    int shift = (int) (frame->edge_turn * 256.0f);
    if (strength < 3) {
        strength = 0;
    }
    if (strength == edge_strength && (strength == 0 || shift == edge_shift)) {
        return;
    }
    edge_strength = strength;
    edge_shift = shift;
    for (int step = 0; step < EDGE_STEPS; step++) {
        const uint8_t *color = edge_palette[(step + shift) & 255];
        int gain = strength * (150 + ((sine[(step * 2 - shift * 3) & 255] * 106) >> 8));
        for (int depth = 0; depth < EDGE_WIDTH; depth++) {
            int level = (edge_fall[depth] * gain) >> 16;
            edge_color[step][depth] =
                pack((color[0] * level) >> 8, (color[1] * level) >> 8, (color[2] * level) >> 8);
        }
    }
    for (int depth = 0; depth < EDGE_LIT; depth++) {
        int low = FX_HEIGHT - 1 - depth;
        uint16_t *top = framebuffer + depth * FX_WIDTH;
        uint16_t *bottom = framebuffer + low * FX_WIDTH;
        for (int x = 0; x < FX_WIDTH; x++) {
            int side = x < FX_WIDTH - 1 - x ? x : FX_WIDTH - 1 - x;
            if (side < depth) {
                top[x] = edge_corner(x, depth, side);
                bottom[x] = edge_corner(x, low, side);
            } else {
                top[x] = edge_color[top_step[x]][depth];
                bottom[x] = edge_color[bottom_step[x]][depth];
            }
        }
    }
    for (int y = EDGE_LIT; y < FX_HEIGHT - EDGE_LIT; y++) {
        uint16_t *row = framebuffer + y * FX_WIDTH;
        const uint16_t *left = edge_color[edge_step(3 * FX_WIDTH + (FX_HEIGHT - 1 - y))];
        const uint16_t *right = edge_color[edge_step(FX_WIDTH + y)];
        for (int x = 0; x < EDGE_LIT; x++) {
            row[x] = left[x];
            row[FX_WIDTH - 1 - x] = right[x];
        }
    }
}

static void place(int logical_y, int *x, int *y)
{
    switch (orientation) {
    case 1:
        *x = logical_y;
        *y = HALF;
        break;
    case 2:
        *x = HALF;
        *y = FX_HEIGHT - logical_y;
        break;
    case 3:
        *x = FX_WIDTH - logical_y;
        *y = HALF;
        break;
    default:
        *x = HALF;
        *y = logical_y;
        break;
    }
}

static inline void put(uint16_t *row, int x, uint16_t value)
{
    if (row && x >= 0 && x < FX_WIDTH) {
        row[x] = value;
    }
}

static void render_span(const fx_frame_t *frame, uint16_t *framebuffer, const span_t *span, const orb_t *orb,
                        const int *rotation, int center_x, int center)
{
    int spin = (4 - orientation) * QUADRANT;
    int low_units = (int) (span->low * QUARTER);
    int high_units = (int) (span->high * QUARTER);
    int hole_units = (int) (frame->hole * QUARTER);
    float inner = (span->low > frame->hole ? span->low : frame->hole) - 1.0f;
    int rows = (int) span->high + 1;
    for (int dy = 0; dy < rows && dy < HALF; dy++) {
        int top = center - 1 - dy;
        int bottom = center + dy;
        uint16_t *upper = top >= 0 && top < FX_HEIGHT ? framebuffer + top * FX_WIDTH : NULL;
        uint16_t *lower = bottom >= 0 && bottom < FX_HEIGHT ? framebuffer + bottom * FX_WIDTH : NULL;
        if (!upper && !lower) {
            continue;
        }
        int first = 0;
        float offset = dy + 0.5f;
        if (inner > offset) {
            first = (int) sqrtf(inner * inner - offset * offset) - 1;
            first = first < 0 ? 0 : first;
        }
        const polar_t *entry = map + dy * HALF + first;
        for (int dx = first; dx < HALF; dx++, entry++) {
            int radius = entry->radius;
            if (radius > high_units) {
                break;
            }
            if (radius < low_units || radius < hole_units) {
                continue;
            }
            int left = center_x - 1 - dx;
            int right = center_x + dx;
            radial_t cell = radial[radius];
            if (cell.band == 0) {
                if (dark[radius]) {
                    continue;
                }
                put(upper, left, 0);
                put(upper, right, 0);
                put(lower, left, 0);
                put(lower, right, 0);
                continue;
            }
            int angle = entry->angle;
            int up_right = angle + spin;
            int up_left = 2 * QUADRANT - angle + spin;
            int down_left = 2 * QUADRANT + angle + spin;
            int down_right = 4 * QUADRANT - angle + spin;
            if (cell.band == ORB_BAND) {
                int index = (radius * orb->scale) >> 12;
                bool flat = index >= ORB_STEPS || orb_lit[index] == 0;
                uint16_t glow = index >= ORB_STEPS ? 0 : orb_glow[index];
                put(upper, right, flat ? glow : shade_orb(orb, index, up_right & ANGLE_MASK));
                put(upper, left, flat ? glow : shade_orb(orb, index, up_left & ANGLE_MASK));
                put(lower, left, flat ? glow : shade_orb(orb, index, down_left & ANGLE_MASK));
                put(lower, right, flat ? glow : shade_orb(orb, index, down_right & ANGLE_MASK));
                continue;
            }
            int band = cell.band - 1;
            const uint8_t *arcs = pattern[band];
            const uint16_t *colors = ramp[band];
            int gain = cell.alpha * 256;
            int turn = rotation[band];
            put(upper, right, colors[(arcs[(up_right + turn) & ANGLE_MASK] * gain) >> 16]);
            put(upper, left, colors[(arcs[(up_left + turn) & ANGLE_MASK] * gain) >> 16]);
            put(lower, left, colors[(arcs[(down_left + turn) & ANGLE_MASK] * gain) >> 16]);
            put(lower, right, colors[(arcs[(down_right + turn) & ANGLE_MASK] * gain) >> 16]);
        }
    }
}

void fx_render(const fx_frame_t *frame, uint16_t *framebuffer)
{
    bool recolored = fabsf(frame->warmth - ramp_warmth) > 0.02f;
    if (recolored) {
        build_ramps(clampf(frame->warmth, 0.0f, 1.0f));
    }
    int center_x;
    int center;
    place((int) (frame->center_y + 0.5f), &center_x, &center);
    bool everything = recolored || !shown_valid || center != shown_center_y || center_x != shown_center_x ||
                      frame->hole != shown_hole;

    span_t spans[MAX_SPANS];
    int span_count = 0;
    bool changed = everything;
    for (int b = 0; b < FX_RINGS; b++) {
        changed |= update_ring(b, &frame->ring[b], everything, spans, &span_count);
    }
    float reach = frame->orb_radius > 1.0f ? frame->orb_radius * ORB_REACH : 0.0f;
    float energy = clampf(frame->orb_energy, 0.0f, 1.0f);
    if (reach > 0.0f || shown_reach > 0.0f) {
        bool halo = everything || (reach > 0.0f) != (shown_reach > 0.0f) ||
                    fabsf(frame->orb_radius - shown_orb) >= RADIUS_STEP || fabsf(energy - shown_energy) >= ENERGY_STEP;
        if (halo) {
            add_span(spans, &span_count, 0.0f, (reach > shown_reach ? reach : shown_reach) + MARGIN);
            shown_orb = frame->orb_radius;
            shown_energy = energy;
            shown_reach = reach;
        } else {
            add_span(spans, &span_count, 0.0f, frame->orb_radius + 1.0f);
        }
        changed = true;
    }
    if (changed) {
        float extent = build_radial(reach);
        if (everything) {
            float moved = shown_valid ? fabsf((float) (center - shown_center_y)) +
                                            fabsf((float) (center_x - shown_center_x))
                                      : HALF;
            float clear = extent > shown_extent + moved ? extent : shown_extent + moved;
            span_count = 0;
            add_span(spans, &span_count, 0.0f, clear + MARGIN);
            memset(dark, 0, sizeof(dark));
        }
        shown_extent = extent;
        shown_center_x = center_x;
        shown_center_y = center;
        shown_hole = frame->hole;
        shown_valid = true;

        int rotation[FX_RINGS];
        for (int b = 0; b < FX_RINGS; b++) {
            rotation[b] = (int) (shown[b].turn * ANGLES) & ANGLE_MASK;
        }
        orb_t orb = {0};
        if (reach > 0.0f) {
            orb.scale = (int) (4096.0f * ORB_UNIT / (frame->orb_radius * QUARTER));
            orb.energy = (int) (256.0f * shown_energy);
            float turn = frame->orb_turn;
            orb.phase[0] = (int) (turn * 256.0f) & 255;
            orb.phase[1] = (int) (-turn * 0.63f * 256.0f) & 255;
            orb.phase[2] = (int) (turn * 0.37f * 256.0f + 80.0f) & 255;
            orb.warm = frame->warmth > 0.5f;
            prepare_orb(&orb);
        }
        span_count = merge_spans(spans, span_count);
        for (int i = 0; i < span_count; i++) {
            render_span(frame, framebuffer, &spans[i], &orb, rotation, center_x, center);
            int last = (int) (spans[i].high * QUARTER);
            for (int index = (int) (spans[i].low * QUARTER) + 1; index < last; index++) {
                dark[index] = radial[index].band == 0;
            }
        }
    }
    render_edge(frame, framebuffer);
}

void fx_invalidate(void)
{
    shown_valid = false;
    edge_strength = -1;
}

void fx_render_orb(uint16_t *framebuffer, int center_y, float radius, float energy, float turn)
{
    float reach = radius * ORB_REACH;
    int last = (int) ((reach + MARGIN + 1.0f) * QUARTER);
    for (int i = 0; i <= last && i < RADIAL_ENTRIES; i++) {
        radial[i].band = (float) i / QUARTER <= reach ? ORB_BAND : 0;
        radial[i].alpha = radial[i].band ? 255 : 0;
        dark[i] = 0;
    }
    orb_t orb = {
        .scale = (int) (4096.0f * ORB_UNIT / (radius * QUARTER)),
        .energy = (int) (256.0f * clampf(energy, 0.0f, 1.0f)),
        .phase = {(int) (turn * 256.0f) & 255, (int) (-turn * 0.63f * 256.0f) & 255,
                  (int) (turn * 0.37f * 256.0f + 80.0f) & 255},
    };
    prepare_orb(&orb);
    fx_frame_t frame = {0};
    span_t span = {0.0f, reach + MARGIN};
    int rotation[FX_RINGS] = {0};
    int x;
    int y;
    place(center_y, &x, &y);
    render_span(&frame, framebuffer, &span, &orb, rotation, x, y);
    shown_valid = false;
}

void fx_set_orientation(int quarter)
{
    orientation = quarter & 3;
    fx_invalidate();
}
