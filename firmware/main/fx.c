#include "fx.h"

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
#define PERIMETER (4 * FX_WIDTH)
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
static uint8_t edge_fall[EDGE_WIDTH];
static uint8_t edge_palette[256][3];
static uint16_t ramp[FX_RINGS][256];
static radial_t radial[RADIAL_ENTRIES];
static float ramp_warmth = -1.0f;
static float last_extent;
static float last_center;
static bool edge_drawn;

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
    last_extent = 0.0f;
    last_center = HALF;
    return true;
}

static float build_radial(const fx_frame_t *frame)
{
    memset(radial, 0, sizeof(radial));
    float extent = 0.0f;
    if (frame->orb_radius > 1.0f) {
        float reach = frame->orb_radius * ORB_REACH;
        int last = (int) (reach * QUARTER);
        for (int i = 0; i <= last && i < RADIAL_ENTRIES; i++) {
            radial[i].band = ORB_BAND;
            radial[i].alpha = 255;
        }
        extent = reach;
    }
    for (int b = 0; b < FX_RINGS; b++) {
        const fx_ring_t *ring = &frame->ring[b];
        if (ring->alpha <= 0.004f || ring->radius < 4.0f) {
            continue;
        }
        const ring_style_t *style = &STYLE[b];
        float reach = style->half_width + 7.0f;
        int first = (int) ((ring->radius - reach) * QUARTER);
        int last = (int) ((ring->radius + reach) * QUARTER);
        for (int i = first < 0 ? 0 : first; i <= last && i < RADIAL_ENTRIES; i++) {
            float distance = fabsf((float) i / QUARTER - ring->radius);
            float core = 1.0f - smooth(style->half_width - 0.5f, style->half_width + 0.5f, distance);
            float glow = style->glow * expf(-(distance - style->half_width) / 2.4f);
            float value = clampf(core > glow ? core : glow, 0.0f, 1.0f) * clampf(ring->alpha, 0.0f, 1.0f);
            uint8_t alpha = (uint8_t) (255.0f * value);
            if (alpha > 2 && (radial[i].band != ORB_BAND || core > 0.2f) && alpha >= radial[i].alpha / 2) {
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

typedef struct {
    int scale;
    int energy;
    int phase[3];
    int warm;
} orb_t;

static inline uint16_t shade_orb(const orb_t *orb, int radius, int angle)
{
    int index = (radius * orb->scale) >> 12;
    if (index >= ORB_STEPS) {
        return 0;
    }
    int theta = angle >> 4;
    int twist = index >> 1;
    int lobe = orb_lobe[index];
    int a = 128 + (((sine[(theta + orb->phase[0] + twist) & 255] - 128) * lobe) >> 8);
    int b = 128 + (((sine[(theta * 2 + orb->phase[1] - twist) & 255] - 128) * lobe) >> 8);
    int c = 96 + (((sine[(theta * 3 + orb->phase[2] + (twist >> 1)) & 255] - 96) * lobe) >> 8);
    int body = orb_body[index];
    int core = (orb_core[index] * orb->energy) >> 8;
    int halo = (orb_halo[index] * orb->energy) >> 8;

    int r = 8 + ((a * 30 + b * 150 + c * 230) >> 9);
    int g = 60 + ((a * 300 + b * 70 + c * 40) >> 9);
    int bl = 190 + ((a * 120 + b * 110 + c * 60) >> 9);
    if (orb->warm) {
        r = 200 + (a >> 2);
        g = 50 + (b >> 3);
        bl = 40 + (c >> 3);
    }
    int lit = (body * (140 + (orb->energy >> 1))) >> 8;
    r = ((r * lit) >> 8) + core + ((halo * (orb->warm ? 255 : 70)) >> 8);
    g = ((g * lit) >> 8) + core + ((halo * (orb->warm ? 90 : 190)) >> 8);
    bl = ((bl * lit) >> 8) + core + ((halo * (orb->warm ? 70 : 255)) >> 8);
    return pack(r, g, bl);
}

static inline uint16_t shade_edge(int x, int y, int shift, int strength)
{
    int vertical = y < FX_HEIGHT - 1 - y ? y : FX_HEIGHT - 1 - y;
    int horizontal = x < FX_WIDTH - 1 - x ? x : FX_WIDTH - 1 - x;
    int depth = vertical < horizontal ? vertical : horizontal;
    int along;
    if (vertical <= horizontal) {
        along = y < HALF ? x : 2 * FX_WIDTH + (FX_WIDTH - 1 - x);
    } else {
        along = x >= HALF ? FX_WIDTH + y : 3 * FX_WIDTH + (FX_HEIGHT - 1 - y);
    }
    const uint8_t *color = edge_palette[(((along * 4369) >> 15) + shift) & 255];
    int wave = 150 + ((sine[(((along * 8738) >> 15) - shift * 3) & 255] * 106) >> 8);
    int level = (edge_fall[depth] * strength * wave) >> 16;
    return pack((color[0] * level) >> 8, (color[1] * level) >> 8, (color[2] * level) >> 8);
}

static void render_edge(const fx_frame_t *frame, uint16_t *framebuffer)
{
    int strength = (int) (256.0f * clampf(frame->edge, 0.0f, 1.0f));
    if (strength < 3 && !edge_drawn) {
        return;
    }
    edge_drawn = strength >= 3;
    int shift = (int) (frame->edge_turn * 256.0f);
    for (int y = 0; y < FX_HEIGHT; y++) {
        uint16_t *row = framebuffer + y * FX_WIDTH;
        if (y < EDGE_WIDTH || y >= FX_HEIGHT - EDGE_WIDTH) {
            for (int x = 0; x < FX_WIDTH; x++) {
                row[x] = shade_edge(x, y, shift, strength);
            }
        } else {
            for (int x = 0; x < EDGE_WIDTH; x++) {
                row[x] = shade_edge(x, y, shift, strength);
                row[FX_WIDTH - 1 - x] = shade_edge(FX_WIDTH - 1 - x, y, shift, strength);
            }
        }
    }
}

void fx_render(const fx_frame_t *frame, uint16_t *framebuffer)
{
    if (fabsf(frame->warmth - ramp_warmth) > 0.02f) {
        build_ramps(clampf(frame->warmth, 0.0f, 1.0f));
    }
    float extent = build_radial(frame);
    int center = (int) (frame->center_y + 0.5f);
    float moved = fabsf((float) center - last_center);
    float clear = extent > last_extent + moved ? extent : last_extent + moved;
    clear += 1.5f;
    if (clear > HALF - 1) {
        clear = HALF - 1;
    }
    last_extent = extent;
    last_center = center;

    int rotation[FX_RINGS];
    int strength[FX_RINGS];
    for (int b = 0; b < FX_RINGS; b++) {
        float turn = frame->ring[b].turn - floorf(frame->ring[b].turn);
        rotation[b] = (int) (turn * ANGLES) & ANGLE_MASK;
        strength[b] = 256;
    }
    orb_t orb = {0};
    if (frame->orb_radius > 1.0f) {
        orb.scale = (int) (4096.0f * ORB_UNIT / (frame->orb_radius * QUARTER));
        orb.energy = (int) (256.0f * clampf(frame->orb_energy, 0.0f, 1.0f));
        float turn = frame->orb_turn;
        orb.phase[0] = (int) (turn * 256.0f) & 255;
        orb.phase[1] = (int) (-turn * 0.63f * 256.0f) & 255;
        orb.phase[2] = (int) (turn * 0.37f * 256.0f + 80.0f) & 255;
        orb.warm = frame->warmth > 0.5f;
    }

    int clear_units = (int) (clear * QUARTER);
    int hole_units = (int) (frame->hole * QUARTER);
    int rows = (int) clear + 1;
    for (int dy = 0; dy < rows && dy < HALF; dy++) {
        int top = center - 1 - dy;
        int bottom = center + dy;
        uint16_t *upper = top >= 0 && top < FX_HEIGHT ? framebuffer + top * FX_WIDTH : NULL;
        uint16_t *lower = bottom >= 0 && bottom < FX_HEIGHT ? framebuffer + bottom * FX_WIDTH : NULL;
        if (!upper && !lower) {
            continue;
        }
        const polar_t *entry = map + dy * HALF;
        for (int dx = 0; dx < HALF; dx++, entry++) {
            int radius = entry->radius;
            if (radius > clear_units) {
                break;
            }
            if (radius < hole_units) {
                continue;
            }
            int left = HALF - 1 - dx;
            int right = HALF + dx;
            radial_t cell = radial[radius];
            if (cell.band == 0) {
                if (upper) {
                    upper[left] = 0;
                    upper[right] = 0;
                }
                if (lower) {
                    lower[left] = 0;
                    lower[right] = 0;
                }
                continue;
            }
            int angle = entry->angle;
            int up_right = angle;
            int up_left = 2 * QUADRANT - angle;
            int down_left = 2 * QUADRANT + angle;
            int down_right = 4 * QUADRANT - angle;
            if (cell.band == ORB_BAND) {
                if (upper) {
                    upper[right] = shade_orb(&orb, radius, up_right);
                    upper[left] = shade_orb(&orb, radius, up_left);
                }
                if (lower) {
                    lower[left] = shade_orb(&orb, radius, down_left);
                    lower[right] = shade_orb(&orb, radius, down_right);
                }
                continue;
            }
            int band = cell.band - 1;
            const uint8_t *arcs = pattern[band];
            const uint16_t *colors = ramp[band];
            int gain = cell.alpha * strength[band];
            int turn = rotation[band];
            if (upper) {
                upper[right] = colors[(arcs[(up_right + turn) & ANGLE_MASK] * gain) >> 16];
                upper[left] = colors[(arcs[(up_left + turn) & ANGLE_MASK] * gain) >> 16];
            }
            if (lower) {
                lower[left] = colors[(arcs[(down_left + turn) & ANGLE_MASK] * gain) >> 16];
                lower[right] = colors[(arcs[(down_right + turn) & ANGLE_MASK] * gain) >> 16];
            }
        }
    }
    render_edge(frame, framebuffer);
}
