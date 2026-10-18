#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "home.h"
#include "lvgl.h"

typedef struct {
    uint32_t at;
    int x;
    int y;
    bool pressed;
} touch_t;

static uint16_t screen[480 * 480];
static uint32_t clock_ms;
static const touch_t *script;
static int script_length;

static uint32_t tick(void)
{
    return clock_ms;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&screen[y * 480 + area->x1], pixels + (size_t) (y - area->y1) * width * 2, (size_t) width * 2);
    }
    lv_display_flush_ready(display);
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    for (int i = 0; i < script_length; i++) {
        if (clock_ms >= script[i].at) {
            data->point.x = script[i].x;
            data->point.y = script[i].y;
            data->state = script[i].pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        }
    }
}

static lv_font_t *load(const char *path, int size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "missing %s\n", path);
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = malloc((size_t) length);
    fread(data, 1, (size_t) length, file);
    fclose(file);
    return lv_tiny_ttf_create_data(data, (size_t) length, size);
}

static void run(uint32_t milliseconds)
{
    uint32_t end = clock_ms + milliseconds;
    while (clock_ms < end) {
        clock_ms += 5;
        lv_timer_handler();
    }
}

static void save(const char *folder, const char *name)
{
    fx_render_orb(screen, HOME_ORB_Y, HOME_ORB_RADIUS, 0.62f, 0.4f);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", folder, name);
    FILE *file = fopen(path, "wb");
    fprintf(file, "P6\n480 480\n255\n");
    for (int i = 0; i < 480 * 480; i++) {
        uint16_t p = screen[i];
        uint8_t rgb[3] = {(uint8_t) (((p >> 11) & 31) * 255 / 31), (uint8_t) (((p >> 5) & 63) * 255 / 63),
                          (uint8_t) ((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

static void action_power(const char *id, bool on) { printf("action: %s %s\n", id, on ? "on" : "off"); }
static void action_level(const char *id, int level) { printf("action: %s level %d\n", id, level); }
static void action_white(const char *id, int kelvin) { printf("action: %s white %d K\n", id, kelvin); }
static void action_hue(const char *id, int hue) { printf("action: %s hue %d\n", id, hue); }
static void action_talk(void) { printf("action: talk\n"); }
static void action_lamp(void) { printf("action: lamp\n"); }
static void action_prefs(const home_prefs_t *prefs)
{
    printf("action: prefs volume %d brightness %d sleep %d dark %d orientation %d sensitivity %d\n", prefs->volume,
           prefs->brightness, prefs->sleep, prefs->dark, prefs->orientation, prefs->sensitivity);
}
static void action_pair(void) { printf("action: pair\n"); }

static void add(home_data_t *data, const char *id, const char *name, const char *room, home_kind_t kind, bool on,
                int level, bool color)
{
    home_device_t *device = &data->devices[data->device_count++];
    snprintf(device->id, sizeof(device->id), "%s", id);
    snprintf(device->name, sizeof(device->name), "%s", name);
    snprintf(device->room, sizeof(device->room), "%s", room);
    device->kind = kind;
    device->on = on;
    device->level = level;
    device->reachable = true;
    device->color = color;
    device->white = kind != HOME_OUTLET;
}

int main(int argc, char **argv)
{
    const char *fonts = argc > 2 ? argv[2] : "firmware/main/fonts";
    const char *folder = argc > 1 ? argv[1] : ".";
    char path[512];
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *display = lv_display_create(480, 480);
    static uint16_t buffer[480 * 480];
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_touch);
    fx_init(malloc);

#define FONT(file, size) (snprintf(path, sizeof(path), "%s/%s", fonts, file), load(path, size))
    home_fonts_t faces = {
        .clock = FONT("inter_200.ttf", 64),
        .temperature = FONT("inter_200.ttf", 44),
        .heading = FONT("inter_500.ttf", 22),
        .body = FONT("inter_500.ttf", 16),
        .small = FONT("inter_400.ttf", 13),
        .caption = FONT("inter_500.ttf", 12),
        .icon = FONT("icons.ttf", 22),
        .icon_large = FONT("icons.ttf", 40),
    };
    home_actions_t actions = {action_power, action_level, action_white, action_hue,
                              action_talk,  action_lamp,  action_prefs, action_pair};
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0), 0);
    home_create(lv_screen_active(), &faces, &actions);

    static home_data_t data;
    snprintf(data.time, sizeof(data.time), "21:52");
    snprintf(data.date, sizeof(data.date), "SOBOTA 17. ŘÍJNA");
    data.weather = true;
    data.celsius = 10;
    data.sky = 2;
    snprintf(data.sky_text, sizeof(data.sky_text), "POLOJASNO");
    snprintf(data.printer, sizeof(data.printer), "45 %% · 1:20");
    snprintf(data.event, sizeof(data.event), "11:00 Doktor");
    data.reminders = 1;
    data.todos = 3;
    snprintf(data.alarm, sizeof(data.alarm), "7:00");
    data.hub = HOME_HUB_READY;
    data.prefs = (home_prefs_t) {.volume = 70, .brightness = 100, .sleep = 1, .dark = 0, .orientation = 0, .sensitivity = 1,
                                  .night = 1, .night_from = 23 * 60, .night_to = 7 * 60};
    snprintf(data.info, sizeof(data.info), "Jarvis 2.4.0 · Wi-Fi −51 dBm · 10.0.0.29");
    add(&data, "a1", "Stropní světlo", "OBÝVÁK", HOME_LIGHT, true, 80, false);
    add(&data, "a2", "LED pásek", "OBÝVÁK", HOME_STRIP, true, 40, true);
    add(&data, "a3", "Stojací lampa", "OBÝVÁK", HOME_LIGHT, false, 100, false);
    add(&data, "b1", "Lampička", "POKOJ", HOME_LIGHT, false, 60, true);
    add(&data, "b2", "Tiskárna", "POKOJ", HOME_OUTLET, true, 0, false);
    add(&data, "c1", "Linka", "KUCHYŇ", HOME_LIGHT, false, 100, false);
    home_update(&data);
    run(200);
    save(folder, "home");

    static const touch_t long_press[] = {{300, 360, 240, true}, {1100, 360, 240, false}};
    script = long_press;
    script_length = 2;
    run(1400);
    save(folder, "sheet");

    static const touch_t close_and_tap[] = {{1700, 46, 46, true}, {1760, 46, 46, false}, {2000, 140, 240, true},
                                            {2060, 140, 240, false}};
    script = close_and_tap;
    script_length = 4;
    run(900);
    save(folder, "toggled");

    static const touch_t settings[] = {{2500, 412, 440, true}, {2560, 412, 440, false}, {2800, 300, 330, true},
                                       {2860, 300, 330, false}};
    script = settings;
    script_length = 4;
    run(500);
    save(folder, "settings");

    static const touch_t leave[] = {{3100, 46, 46, true}, {3160, 46, 46, false}, {3400, 240, 440, true},
                                    {3460, 240, 440, false}};
    script = leave;
    script_length = 4;
    run(700);

    static home_data_t empty;
    empty = data;
    empty.device_count = 0;
    empty.hub = HOME_HUB_OFFER;
    empty.printer[0] = 0;
    empty.reminders = 0;
    home_update(&empty);
    run(200);
    save(folder, "pairing");
    return 0;
}
