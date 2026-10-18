#include "home.h"

#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "memory.h"

#define COLOR_BACKGROUND 0x000000
#define COLOR_PANEL 0x0B1219
#define COLOR_PANEL_EDGE 0x17222C
#define COLOR_PANEL_PRESSED 0x14202B
#define COLOR_PANEL_ON 0x0C2430
#define COLOR_EDGE_ON 0x2F86AD
#define COLOR_ACCENT 0x5BD7FF
#define COLOR_TEXT 0xF5F9FF
#define COLOR_MUTED 0x8DA2B5
#define COLOR_DIM 0x51606E
#define COLOR_INK 0x031018
#define COLOR_BADGE 0x17232D
#define COLOR_LINE 0x101B24

#define SIDE 24
#define TILE_WIDTH 210
#define TILE_HEIGHT 64
#define GRID_TOP 172
#define GRID_BOTTOM 400
#define PENDING_MS 4000

typedef struct {
    lv_obj_t *tile;
    lv_obj_t *badge;
    lv_obj_t *glyph;
    lv_obj_t *name;
    lv_obj_t *state;
    uint32_t pending_until;
} tile_t;

typedef struct {
    int kelvin;
    int hue;
    uint32_t color;
} swatch_t;

#define MINUTES_PER_DAY 1440
#define NIGHT_STEP_MINUTES 30

static const char *const NIGHT_LABELS[] = {"vypnuto", "zapnuto"};

const int HOME_SLEEP_SECONDS[HOME_SLEEP_CHOICES] = {30, 60, 300, 900, 0};

static const char *const SLEEP_LABELS[HOME_SLEEP_CHOICES] = {"30 s", "1 min", "5 min", "15 min", "nikdy"};
static const char *const AFTER_LABELS[] = {"hodiny", "tma"};
static const char *const ORIENTATION_LABELS[] = {"0°", "90°", "180°", "270°"};
static const char *const SENSITIVITY_LABELS[] = {"nízká", "střední", "vysoká"};

static const swatch_t SWATCHES[] = {
    {2700, -1, 0xFFC36B}, {4000, -1, 0xFFF0D4}, {6500, -1, 0xDCE9FF}, {0, 0, 0xFF4B3E},
    {0, 30, 0xFF9A2E},    {0, 55, 0xFFE14D},    {0, 120, 0x46E06A},   {0, 240, 0x3D7BFF},
    {0, 275, 0xA65BFF},   {0, 320, 0xFF5CC8},
};

static home_fonts_t font;
static home_actions_t act;
static lv_obj_t *root;
static lv_obj_t *time_label;
static lv_obj_t *date_label;
static lv_obj_t *weather_icon;
static lv_obj_t *weather_temperature;
static lv_obj_t *weather_text;
static lv_obj_t *pill_row;
static lv_obj_t *grid;
static lv_obj_t *sheet;
static lv_obj_t *sheet_value;
static lv_obj_t *sheet_power;
static tile_t tiles[HOME_DEVICES];
static int tile_count;
static char sheet_id[56];
static int sheet_device = -1;
static EXTERNAL_RAM home_data_t shown;
static home_prefs_t editing;
static bool built;

static void build_pills(const home_data_t *data);

static void style_panel(lv_obj_t *object, int radius)
{
    lv_obj_remove_style_all(object);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(COLOR_PANEL_PRESSED), LV_STATE_PRESSED);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *face, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, face, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, int size, const char *glyph, lv_event_cb_t callback, void *data)
{
    lv_obj_t *button = lv_obj_create(parent);
    style_panel(button, size / 2);
    lv_obj_set_size(button, size, size);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *label = make_label(button, font.icon, COLOR_MUTED, glyph);
    lv_obj_center(label);
    return button;
}

static const char *sky_glyph(int code)
{
    if (code <= 1) {
        return ICON_WEATHER_SUNNY;
    }
    if (code == 2) {
        return ICON_WEATHER_PARTLY_CLOUDY;
    }
    if (code == 3) {
        return ICON_WEATHER_CLOUDY;
    }
    if (code == 45 || code == 48) {
        return ICON_WEATHER_FOG;
    }
    if (code == 65 || (code >= 80 && code <= 82)) {
        return ICON_WEATHER_POURING;
    }
    if (code == 66 || code == 67) {
        return ICON_WEATHER_SNOWY_RAINY;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return ICON_WEATHER_SNOWY;
    }
    if (code >= 95) {
        return ICON_WEATHER_LIGHTNING;
    }
    return ICON_WEATHER_RAINY;
}

static const char *device_glyph(const home_device_t *device)
{
    if (device->kind == HOME_OUTLET) {
        return ICON_POWER_SOCKET_EU;
    }
    if (device->kind == HOME_STRIP) {
        return ICON_LED_STRIP_VARIANT;
    }
    return device->on ? ICON_LIGHTBULB : ICON_LIGHTBULB_OUTLINE;
}

static void describe_state(const home_device_t *device, char *out, size_t capacity)
{
    if (!device->reachable) {
        snprintf(out, capacity, "Nedostupné");
    } else if (!device->on) {
        snprintf(out, capacity, "Vypnuto");
    } else if (device->kind != HOME_OUTLET && device->level > 0) {
        snprintf(out, capacity, "Zapnuto · %d %%", device->level);
    } else {
        snprintf(out, capacity, "Zapnuto");
    }
}

static void paint_tile(int index)
{
    const home_device_t *device = &shown.devices[index];
    tile_t *tile = &tiles[index];
    bool lit = device->on && device->reachable;
    char state[32];
    describe_state(device, state, sizeof(state));
    lv_obj_set_style_bg_color(tile->tile, lv_color_hex(lit ? COLOR_PANEL_ON : COLOR_PANEL), 0);
    lv_obj_set_style_border_color(tile->tile, lv_color_hex(lit ? COLOR_EDGE_ON : COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_bg_color(tile->badge, lv_color_hex(lit ? COLOR_ACCENT : COLOR_BADGE), 0);
    lv_obj_set_style_text_color(tile->glyph, lv_color_hex(lit ? COLOR_INK : COLOR_MUTED), 0);
    lv_label_set_text(tile->glyph, device_glyph(device));
    lv_label_set_text(tile->state, state);
    lv_obj_set_style_text_color(tile->state, lv_color_hex(lit ? COLOR_ACCENT : COLOR_MUTED), 0);
    lv_obj_set_style_text_color(tile->name, lv_color_hex(device->reachable ? COLOR_TEXT : COLOR_DIM), 0);
}

static void close_sheet(void)
{
    if (sheet) {
        lv_obj_delete(sheet);
        sheet = NULL;
        sheet_value = NULL;
        sheet_power = NULL;
        sheet_device = -1;
    }
}

static void on_close(lv_event_t *event)
{
    close_sheet();
}

static lv_obj_t *open_sheet(const char *title)
{
    close_sheet();
    sheet = lv_obj_create(root);
    lv_obj_remove_style_all(sheet);
    lv_obj_set_size(sheet, 480, 480);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *close = make_button(sheet, 52, ICON_CLOSE, on_close, NULL);
    lv_obj_set_pos(close, 20, 20);
    lv_obj_t *heading = make_label(sheet, font.heading, COLOR_TEXT, title);
    lv_obj_set_width(heading, 340);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 32);
    return sheet;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int width, int height, int value)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, width, height);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    int radius = (width < height ? width : height) / 2;
    lv_obj_set_style_radius(slider, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_BADGE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, lv_color_hex(COLOR_PANEL_EDGE), LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_clip_corner(slider, true, LV_PART_MAIN);
    return slider;
}

static void show_level(int level)
{
    if (sheet_value) {
        lv_label_set_text_fmt(sheet_value, "%d %%", level);
    }
}

static void on_level_change(lv_event_t *event)
{
    show_level(lv_slider_get_value(lv_event_get_target_obj(event)));
}

static void on_level_release(lv_event_t *event)
{
    int level = lv_slider_get_value(lv_event_get_target_obj(event));
    if (level < 1) {
        level = 1;
        lv_slider_set_value(lv_event_get_target_obj(event), 1, LV_ANIM_OFF);
        show_level(1);
    }
    if (sheet_device >= 0) {
        shown.devices[sheet_device].level = level;
        shown.devices[sheet_device].on = true;
        tiles[sheet_device].pending_until = lv_tick_get() + PENDING_MS;
        paint_tile(sheet_device);
    }
    if (act.brightness) {
        act.brightness(sheet_id, level);
    }
}

static void paint_power(bool on)
{
    if (!sheet_power) {
        return;
    }
    lv_obj_set_style_bg_color(sheet_power, lv_color_hex(on ? COLOR_ACCENT : COLOR_PANEL), 0);
    lv_obj_set_style_border_color(sheet_power, lv_color_hex(on ? COLOR_ACCENT : COLOR_PANEL_EDGE), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(sheet_power, 0), lv_color_hex(on ? COLOR_INK : COLOR_MUTED), 0);
}

static void set_power(int index, bool on)
{
    home_device_t *device = &shown.devices[index];
    device->on = on;
    tiles[index].pending_until = lv_tick_get() + PENDING_MS;
    paint_tile(index);
    build_pills(&shown);
    if (index == sheet_device) {
        paint_power(on);
    }
    if (act.power) {
        act.power(device->id, on);
    }
}

static void on_power(lv_event_t *event)
{
    if (sheet_device >= 0) {
        set_power(sheet_device, !shown.devices[sheet_device].on);
    }
}

static void on_swatch(lv_event_t *event)
{
    const swatch_t *swatch = lv_event_get_user_data(event);
    if (sheet_device < 0) {
        return;
    }
    shown.devices[sheet_device].on = true;
    tiles[sheet_device].pending_until = lv_tick_get() + PENDING_MS;
    paint_tile(sheet_device);
    paint_power(true);
    if (swatch->kelvin && act.white) {
        act.white(sheet_id, swatch->kelvin);
    } else if (!swatch->kelvin && act.hue) {
        act.hue(sheet_id, swatch->hue);
    }
}

static void open_device(int index)
{
    const home_device_t *device = &shown.devices[index];
    lv_obj_t *panel = open_sheet(device->name);
    sheet_device = index;
    strlcpy(sheet_id, device->id, sizeof(sheet_id));
    lv_obj_t *room = make_label(panel, font.caption, COLOR_MUTED, device->room);
    lv_obj_set_style_text_letter_space(room, 3, 0);
    lv_obj_align(room, LV_ALIGN_TOP_MID, 0, 66);

    sheet_power = lv_obj_create(panel);
    style_panel(sheet_power, 40);
    lv_obj_set_size(sheet_power, 80, 80);
    lv_obj_add_flag(sheet_power, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sheet_power, on_power, LV_EVENT_CLICKED, NULL);
    lv_obj_t *glyph = make_label(sheet_power, font.icon_large, COLOR_MUTED, ICON_POWER);
    lv_obj_center(glyph);

    if (device->kind == HOME_OUTLET) {
        lv_obj_align(sheet_power, LV_ALIGN_CENTER, 0, 20);
        paint_power(device->on);
        return;
    }
    lv_obj_set_pos(sheet_power, 300, 112);
    paint_power(device->on);

    int level = device->level > 0 ? device->level : 100;
    lv_obj_t *slider = make_slider(panel, 120, 300, device->on ? level : 0);
    lv_obj_set_pos(slider, 64, 112);
    lv_obj_add_event_cb(slider, on_level_change, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, on_level_release, LV_EVENT_RELEASED, NULL);
    sheet_value = make_label(panel, font.heading, COLOR_TEXT, "");
    lv_obj_set_width(sheet_value, 120);
    lv_obj_set_style_text_align(sheet_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sheet_value, 64, 424);
    show_level(device->on ? level : 0);

    int column = 0;
    int row = 0;
    for (size_t i = 0; i < sizeof(SWATCHES) / sizeof(SWATCHES[0]); i++) {
        const swatch_t *swatch = &SWATCHES[i];
        if ((swatch->kelvin && !device->white && !device->color) || (!swatch->kelvin && !device->color)) {
            continue;
        }
        lv_obj_t *dot = lv_obj_create(panel);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 48, 48);
        lv_obj_set_style_radius(dot, 24, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(swatch->color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 3, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(dot, lv_color_hex(COLOR_TEXT), LV_STATE_PRESSED);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(dot, on_swatch, LV_EVENT_CLICKED, (void *) swatch);
        lv_obj_set_pos(dot, 236 + column * 58, 220 + row * 58);
        if (++column == 4) {
            column = 0;
            row++;
        }
    }
}

static void on_tile_tap(lv_event_t *event)
{
    int index = (int) (intptr_t) lv_event_get_user_data(event);
    if (index < shown.device_count && shown.devices[index].reachable) {
        set_power(index, !shown.devices[index].on);
    }
}

static void on_tile_hold(lv_event_t *event)
{
    int index = (int) (intptr_t) lv_event_get_user_data(event);
    if (index < shown.device_count) {
        open_device(index);
    }
}

static void on_pair(lv_event_t *event)
{
    if (act.pair) {
        act.pair();
    }
}

static void on_talk(lv_event_t *event)
{
    if (act.talk) {
        act.talk();
    }
}

static void on_lamp(lv_event_t *event)
{
    if (act.lamp) {
        act.lamp();
    }
}

static void send_prefs(void)
{
    shown.prefs = editing;
    if (act.prefs) {
        act.prefs(&editing);
    }
}

static void paint_choices(lv_obj_t *row, int selected)
{
    uint32_t count = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *pill = lv_obj_get_child(row, (int32_t) i);
        bool chosen = (int) i == selected;
        lv_obj_set_style_bg_color(pill, lv_color_hex(chosen ? COLOR_ACCENT : COLOR_PANEL), 0);
        lv_obj_set_style_border_color(pill, lv_color_hex(chosen ? COLOR_ACCENT : COLOR_PANEL_EDGE), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(pill, 0), lv_color_hex(chosen ? COLOR_INK : COLOR_TEXT), 0);
    }
}

static void on_choice(lv_event_t *event)
{
    lv_obj_t *pill = lv_event_get_current_target_obj(event);
    lv_obj_t *row = lv_obj_get_parent(pill);
    int *target = lv_obj_get_user_data(row);
    int index = (int) (intptr_t) lv_event_get_user_data(event);
    *target = index;
    paint_choices(row, index);
    send_prefs();
}

static lv_obj_t *settings_title(lv_obj_t *list, const char *glyph, const char *title)
{
    lv_obj_t *line = lv_obj_create(list);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 8, 0);
    lv_obj_set_style_pad_top(line, 6, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    make_label(line, font.icon, COLOR_ACCENT, glyph);
    lv_obj_t *label = make_label(line, font.caption, COLOR_MUTED, title);
    lv_obj_set_style_text_letter_space(label, 3, 0);
    return line;
}

static void make_choices(lv_obj_t *list, const char *glyph, const char *title, const char *const *labels, int count,
                         int *target)
{
    settings_title(list, glyph, title);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(row, target);
    for (int i = 0; i < count; i++) {
        lv_obj_t *pill = lv_obj_create(row);
        style_panel(pill, 20);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, 40);
        lv_obj_set_style_pad_left(pill, 16, 0);
        lv_obj_set_style_pad_right(pill, 16, 0);
        lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(pill, on_choice, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_t *label = make_label(pill, font.small, COLOR_TEXT, labels[i]);
        lv_obj_center(label);
    }
    paint_choices(row, *target);
}

static void on_slide(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    lv_obj_t *label = lv_obj_get_user_data(slider);
    lv_label_set_text_fmt(label, "%d %%", (int) lv_slider_get_value(slider));
}

static void on_slid(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    int *target = lv_event_get_user_data(event);
    *target = (int) lv_slider_get_value(slider);
    send_prefs();
}

static void make_level(lv_obj_t *list, const char *glyph, const char *title, int minimum, int *target)
{
    settings_title(list, glyph, title);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *slider = make_slider(row, 344, 40, *target);
    lv_slider_set_range(slider, minimum, 100);
    lv_slider_set_value(slider, *target, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value = make_label(row, font.body, COLOR_TEXT, "");
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text_fmt(value, "%d %%", *target);
    lv_obj_set_user_data(slider, value);
    lv_obj_add_event_cb(slider, on_slide, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, on_slid, LV_EVENT_RELEASED, target);
}

static void show_minutes(lv_obj_t *label, int minutes)
{
    lv_label_set_text_fmt(label, "%d:%02d", minutes / 60, minutes % 60);
}

static void on_step(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target_obj(event);
    lv_obj_t *value = lv_obj_get_user_data(button);
    int *target = lv_obj_get_user_data(value);
    int step = (int) (intptr_t) lv_event_get_user_data(event);
    *target = ((*target + step) % MINUTES_PER_DAY + MINUTES_PER_DAY) % MINUTES_PER_DAY;
    show_minutes(value, *target);
    send_prefs();
}

static lv_obj_t *make_step_button(lv_obj_t *parent, const char *text, lv_obj_t *value, int step)
{
    lv_obj_t *button = lv_obj_create(parent);
    style_panel(button, 20);
    lv_obj_set_size(button, 40, 40);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(button, value);
    lv_obj_add_event_cb(button, on_step, LV_EVENT_CLICKED, (void *) (intptr_t) step);
    lv_obj_center(make_label(button, font.body, COLOR_TEXT, text));
    return button;
}

static void make_clock_step(lv_obj_t *row, const char *caption, int *target)
{
    lv_obj_t *group = lv_obj_create(row);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, 40);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, 8, 0);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    make_label(group, font.small, COLOR_MUTED, caption);
    lv_obj_t *value = lv_label_create(group);
    lv_obj_set_style_text_font(value, font.body, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_width(value, 52);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_user_data(value, target);
    show_minutes(value, *target);
    lv_obj_move_to_index(make_step_button(group, "–", value, -NIGHT_STEP_MINUTES), 1);
    make_step_button(group, "+", value, NIGHT_STEP_MINUTES);
}

static void make_night(lv_obj_t *list)
{
    make_choices(list, ICON_MOON_WANING_CRESCENT, "NOČNÍ REŽIM — DISPLEJ ZHASNE", NIGHT_LABELS, 2, &editing.night);
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    make_clock_step(row, "OD", &editing.night_from);
    make_clock_step(row, "DO", &editing.night_to);
}

static void on_settings(lv_event_t *event)
{
    lv_obj_t *panel = open_sheet("Nastavení");
    editing = shown.prefs;
    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 480, 480 - 88);
    lv_obj_set_pos(list, 0, 88);
    lv_obj_set_style_pad_left(list, SIDE, 0);
    lv_obj_set_style_pad_right(list, SIDE, 0);
    lv_obj_set_style_pad_bottom(list, 24, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    make_level(list, ICON_VOLUME_HIGH, "HLASITOST", 0, &editing.volume);
    make_level(list, ICON_BRIGHTNESS_6, "JAS DISPLEJE", 10, &editing.brightness);
    make_choices(list, ICON_POWER_SLEEP, "USPAT PO", SLEEP_LABELS, HOME_SLEEP_CHOICES, &editing.sleep);
    make_choices(list, ICON_WEATHER_NIGHT, "PO USPÁNÍ", AFTER_LABELS, 2, &editing.dark);
    make_night(list);
    make_choices(list, ICON_SCREEN_ROTATION, "ORIENTACE", ORIENTATION_LABELS, 4, &editing.orientation);
    make_choices(list, ICON_EAR_HEARING, "CITLIVOST NA OSLOVENÍ", SENSITIVITY_LABELS, 3, &editing.sensitivity);
    settings_title(list, ICON_INFORMATION_OUTLINE, "O ZAŘÍZENÍ");
    lv_obj_t *info = make_label(list, font.small, COLOR_MUTED, shown.info);
    lv_obj_set_width(info, lv_pct(100));
    lv_label_set_long_mode(info, LV_LABEL_LONG_MODE_WRAP);
}

static lv_obj_t *make_tile(int index)
{
    tile_t *tile = &tiles[index];
    const home_device_t *device = &shown.devices[index];
    tile->tile = lv_obj_create(grid);
    style_panel(tile->tile, 20);
    lv_obj_set_size(tile->tile, TILE_WIDTH, TILE_HEIGHT);
    lv_obj_add_flag(tile->tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile->tile, on_tile_tap, LV_EVENT_SHORT_CLICKED, (void *) (intptr_t) index);
    lv_obj_add_event_cb(tile->tile, on_tile_hold, LV_EVENT_LONG_PRESSED, (void *) (intptr_t) index);

    tile->badge = lv_obj_create(tile->tile);
    lv_obj_remove_style_all(tile->badge);
    lv_obj_set_size(tile->badge, 38, 38);
    lv_obj_set_style_radius(tile->badge, 19, 0);
    lv_obj_set_style_bg_opa(tile->badge, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tile->badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(tile->badge, 12, 13);
    tile->glyph = make_label(tile->badge, font.icon, COLOR_MUTED, "");
    lv_obj_center(tile->glyph);

    tile->name = make_label(tile->tile, font.body, COLOR_TEXT, device->name);
    lv_obj_set_width(tile->name, 136);
    lv_label_set_long_mode(tile->name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(tile->name, 62, 11);
    tile->state = make_label(tile->tile, font.small, COLOR_MUTED, "");
    lv_obj_set_width(tile->state, 136);
    lv_label_set_long_mode(tile->state, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(tile->state, 62, 34);
    tile->pending_until = 0;
    paint_tile(index);
    return tile->tile;
}

static void make_room(const char *room)
{
    lv_obj_t *label = make_label(grid, font.caption, COLOR_DIM, room);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_letter_space(label, 3, 0);
    lv_obj_set_style_pad_top(label, 4, 0);
}

static void make_notice(const char *glyph, const char *title, const char *detail, bool clickable)
{
    lv_obj_t *card = lv_obj_create(grid);
    style_panel(card, 20);
    lv_obj_set_size(card, TILE_WIDTH * 2 + 12, 76);
    if (clickable) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_pair, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_border_color(card, lv_color_hex(COLOR_EDGE_ON), 0);
    } else {
        lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *icon = make_label(card, font.icon_large, COLOR_ACCENT, glyph);
    lv_obj_set_pos(icon, 16, 18);
    lv_obj_t *heading = make_label(card, font.body, COLOR_TEXT, title);
    lv_obj_set_pos(heading, 72, 14);
    lv_obj_t *text = make_label(card, font.small, COLOR_MUTED, detail);
    lv_obj_set_width(text, 340);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(text, 72, 40);
}

static bool same_layout(const home_data_t *data)
{
    if (!built || data->device_count != shown.device_count || data->hub != shown.hub) {
        return false;
    }
    for (int i = 0; i < data->device_count; i++) {
        const home_device_t *a = &data->devices[i];
        const home_device_t *b = &shown.devices[i];
        if (strcmp(a->id, b->id) || strcmp(a->name, b->name) || strcmp(a->room, b->room) || a->kind != b->kind) {
            return false;
        }
    }
    return true;
}

static void build_grid(void)
{
    lv_obj_clean(grid);
    tile_count = 0;
    if (shown.hub == HOME_HUB_OFFER) {
        make_notice(ICON_LINK_VARIANT, "Spárovat IKEA hub", "Dotkni se a do minuty stiskni tlačítko na spodku hubu", true);
    } else if (shown.hub == HOME_HUB_PAIRING) {
        make_notice(ICON_LINK_VARIANT, "Párování běží", "Stiskni tlačítko na spodku hubu", false);
    } else if (shown.device_count == 0) {
        make_notice(ICON_HOME_VARIANT_OUTLINE, "Zatím žádná zařízení",
                    shown.hub == HOME_HUB_READY ? "Hub je spárovaný, ale nic nehlásí" : "IKEA hub jsem v síti nenašel", false);
    }
    const char *room = NULL;
    for (int i = 0; i < shown.device_count; i++) {
        if (!room || strcmp(room, shown.devices[i].room) != 0) {
            room = shown.devices[i].room;
            make_room(room[0] ? room : "OSTATNÍ");
        }
        make_tile(i);
        tile_count++;
    }
}

static void make_pill(const char *glyph, const char *text)
{
    lv_obj_t *pill = lv_obj_create(pill_row);
    style_panel(pill, 16);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_pad_left(pill, 12, 0);
    lv_obj_set_style_pad_right(pill, 14, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 6, 0);
    make_label(pill, font.icon, COLOR_ACCENT, glyph);
    make_label(pill, font.small, COLOR_TEXT, text);
}

static void build_pills(const home_data_t *data)
{
    lv_obj_clean(pill_row);
    int lit = 0;
    for (int i = 0; i < data->device_count; i++) {
        lit += data->devices[i].on && data->devices[i].reachable && data->devices[i].kind != HOME_OUTLET;
    }
    char text[96];
    if (data->hub == HOME_HUB_READY && data->device_count > 0) {
        if (lit == 0) {
            snprintf(text, sizeof(text), "vše zhasnuto");
        } else {
            snprintf(text, sizeof(text), "%d svítí", lit);
        }
        make_pill(lit ? ICON_LIGHTBULB : ICON_LIGHTBULB_OUTLINE, text);
    }
    if (data->alarm[0]) {
        make_pill(ICON_ALARM, data->alarm);
    }
    if (data->todos > 0) {
        snprintf(text, sizeof(text), "%d %s", data->todos,
                 data->todos == 1 ? "úkol" : data->todos < 5 ? "úkoly" : "úkolů");
        make_pill(ICON_FORMAT_LIST_CHECKS, text);
        if (data->event[0]) {
        make_pill(ICON_CALENDAR_BLANK, data->event);
    }
    if (data->reminders > 0) {
        snprintf(text, sizeof(text), "%d %s", data->reminders,
                 data->reminders == 1 ? "připomínka" : data->reminders < 5 ? "připomínky" : "připomínek");
        make_pill(ICON_BELL_RING, text);
    }
    if (data->printer[0]) {
        make_pill(ICON_PRINTER_3D, data->printer);
    }
}
}

lv_obj_t *home_create(lv_obj_t *parent, const home_fonts_t *fonts, const home_actions_t *actions)
{
    font = *fonts;
    act = *actions;
    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 480, 480);
    lv_obj_set_style_bg_color(root, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *brand = make_label(root, font.caption, COLOR_DIM, "J.A.R.V.I.S.");
    lv_obj_set_style_text_letter_space(brand, 3, 0);
    lv_obj_set_pos(brand, SIDE + 2, 20);

    time_label = make_label(root, font.clock, COLOR_TEXT, "--:--");
    lv_obj_set_pos(time_label, SIDE - 2, 34);
    date_label = make_label(root, font.caption, COLOR_MUTED, "");
    lv_obj_set_style_text_letter_space(date_label, 3, 0);
    lv_obj_set_pos(date_label, SIDE + 2, 108);

    weather_temperature = make_label(root, font.temperature, COLOR_TEXT, "");
    lv_obj_align(weather_temperature, LV_ALIGN_TOP_RIGHT, -SIDE, 44);
    weather_icon = make_label(root, font.icon_large, COLOR_ACCENT, "");
    lv_obj_align_to(weather_icon, weather_temperature, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    weather_text = make_label(root, font.caption, COLOR_MUTED, "");
    lv_obj_set_style_text_letter_space(weather_text, 3, 0);
    lv_obj_align(weather_text, LV_ALIGN_TOP_RIGHT, -SIDE, 108);

    pill_row = lv_obj_create(root);
    lv_obj_remove_style_all(pill_row);
    lv_obj_set_size(pill_row, 480, 32);
    lv_obj_set_pos(pill_row, 0, 128);
    lv_obj_set_style_pad_left(pill_row, SIDE, 0);
    lv_obj_set_style_pad_right(pill_row, SIDE, 0);
    lv_obj_set_flex_flow(pill_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pill_row, 8, 0);
    lv_obj_set_scroll_dir(pill_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(pill_row, LV_SCROLLBAR_MODE_OFF);

    grid = lv_obj_create(root);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 480, GRID_BOTTOM - GRID_TOP);
    lv_obj_set_pos(grid, 0, GRID_TOP);
    lv_obj_set_style_pad_left(grid, SIDE, 0);
    lv_obj_set_style_pad_right(grid, SIDE, 0);
    lv_obj_set_style_pad_bottom(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *line = lv_obj_create(root);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 480 - 2 * SIDE, 1);
    lv_obj_set_pos(line, SIDE, GRID_BOTTOM + 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lamp = make_button(root, 56, ICON_LAMP, on_lamp, NULL);
    lv_obj_set_pos(lamp, SIDE + 8, HOME_ORB_Y - 28);
    lv_obj_t *gear = make_button(root, 56, ICON_COG_OUTLINE, on_settings, NULL);
    lv_obj_set_pos(gear, 480 - SIDE - 8 - 56, HOME_ORB_Y - 28);

    lv_obj_t *talk = lv_obj_create(root);
    lv_obj_remove_style_all(talk);
    lv_obj_set_size(talk, 120, 72);
    lv_obj_set_pos(talk, HOME_ORB_X - 60, HOME_ORB_Y - 36);
    lv_obj_add_flag(talk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(talk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(talk, on_talk, LV_EVENT_CLICKED, NULL);

    memset(&shown, 0, sizeof(shown));
    built = false;
    return root;
}

void home_update(const home_data_t *data)
{
    if (strcmp(data->time, shown.time) != 0) {
        lv_label_set_text(time_label, data->time);
    }
    if (strcmp(data->date, shown.date) != 0) {
        lv_label_set_text(date_label, data->date);
    }
    if (data->weather != shown.weather || data->celsius != shown.celsius || data->sky != shown.sky ||
        strcmp(data->sky_text, shown.sky_text) != 0 || !built) {
        if (data->weather) {
            lv_label_set_text_fmt(weather_temperature, "%d°", data->celsius);
            lv_label_set_text(weather_icon, sky_glyph(data->sky));
            lv_label_set_text(weather_text, data->sky_text);
        } else {
            lv_label_set_text(weather_temperature, "");
            lv_label_set_text(weather_icon, "");
            lv_label_set_text(weather_text, "");
        }
        lv_obj_align(weather_temperature, LV_ALIGN_TOP_RIGHT, -SIDE, 44);
        lv_obj_update_layout(weather_temperature);
        lv_obj_align_to(weather_icon, weather_temperature, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        lv_obj_align(weather_text, LV_ALIGN_TOP_RIGHT, -SIDE, 108);
    }
    bool pills = !built || strcmp(data->printer, shown.printer) || strcmp(data->event, shown.event) ||
                 data->reminders != shown.reminders || data->todos != shown.todos ||
                 strcmp(data->alarm, shown.alarm) || data->hub != shown.hub ||
                 data->device_count != shown.device_count;
    for (int i = 0; i < data->device_count && !pills; i++) {
        pills = data->devices[i].on != shown.devices[i].on || data->devices[i].reachable != shown.devices[i].reachable;
    }
    if (pills) {
        build_pills(data);
    }
    static EXTERNAL_RAM home_device_t kept[HOME_DEVICES];
    static EXTERNAL_RAM home_device_t previous[HOME_DEVICES];
    bool layout = same_layout(data);
    uint32_t now = lv_tick_get();
    int kept_count = 0;
    if (layout) {
        kept_count = data->device_count;
        for (int i = 0; i < kept_count; i++) {
            kept[i] = tiles[i].pending_until && now < tiles[i].pending_until ? shown.devices[i] : data->devices[i];
        }
    }
    memcpy(previous, shown.devices, sizeof(previous));
    shown = *data;
    if (!layout) {
        if (sheet) {
            close_sheet();
        }
        build_grid();
        built = true;
        return;
    }
    for (int i = 0; i < kept_count; i++) {
        shown.devices[i] = kept[i];
        const home_device_t *was = &previous[i];
        if (was->on != kept[i].on || was->level != kept[i].level || was->reachable != kept[i].reachable) {
            paint_tile(i);
        }
    }
}

void home_close_sheet(void)
{
    close_sheet();
}

bool home_sheet_open(void)
{
    return sheet != NULL;
}
