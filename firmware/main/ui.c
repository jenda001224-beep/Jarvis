#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "net.h"
#include "wake.h"

#define COLOR_BACKGROUND 0x05080C
#define COLOR_ACCENT 0x19C8FF
#define COLOR_ACCENT_DEEP 0x0A4DFF
#define COLOR_ERROR 0xFF4D5E
#define COLOR_TEXT 0xF2F6FA
#define COLOR_MUTED 0x7C8B99
#define COLOR_DIM 0x5B6B7A
#define COLOR_FAINT 0x3F4D5B
#define COLOR_CARD 0x0E141B
#define COLOR_LINE 0x1C2733

#define HEARD_LIMIT 42
#define SPOKEN_LIMIT 150
#define BRIGHTNESS_ACTIVE 100
#define BRIGHTNESS_IDLE 45
#define DIM_AFTER_MS 60000

#define ICON_MICROPHONE "\xF3\xB0\x8D\xAC"

extern const uint8_t inter_200_ttf_start[] asm("_binary_inter_200_ttf_start");
extern const uint8_t inter_200_ttf_end[] asm("_binary_inter_200_ttf_end");
extern const uint8_t inter_400_ttf_start[] asm("_binary_inter_400_ttf_start");
extern const uint8_t inter_400_ttf_end[] asm("_binary_inter_400_ttf_end");
extern const uint8_t inter_500_ttf_start[] asm("_binary_inter_500_ttf_start");
extern const uint8_t inter_500_ttf_end[] asm("_binary_inter_500_ttf_end");
extern const uint8_t inter_clock_ttf_start[] asm("_binary_inter_clock_ttf_start");
extern const uint8_t inter_clock_ttf_end[] asm("_binary_inter_clock_ttf_end");
extern const uint8_t icons_ttf_start[] asm("_binary_icons_ttf_start");
extern const uint8_t icons_ttf_end[] asm("_binary_icons_ttf_end");

static lv_obj_t *clock_label;
static lv_obj_t *date_label;
static lv_obj_t *hint_label;
static lv_obj_t *status_dot;
static lv_obj_t *hub_button;
static lv_obj_t *hub_label;
static lv_obj_t *overlay;
static lv_obj_t *ring_outer;
static lv_obj_t *ring_middle;
static lv_obj_t *ring_core;
static lv_obj_t *state_label;
static lv_obj_t *heard_label;
static lv_obj_t *spoken_label;

static ui_mode_t mode = UI_STARTING;
static char heard[HEARD_LIMIT + 8];
static char spoken[SPOKEN_LIMIT + 8];
static float level;
static int last_core;
static int last_minute = -1;
static int brightness = -1;

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_ring(lv_obj_t *parent, int size, int border, lv_opa_t opacity)
{
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, size, size);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, border, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(ring, opacity, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return ring;
}

static void set_brightness(int value)
{
    if (value != brightness) {
        brightness = value;
        bsp_display_brightness_set(value);
    }
}

static void append_tail(char *buffer, size_t limit, const char *text)
{
    size_t have = strlen(buffer);
    size_t add = strlen(text);
    if (add > limit) {
        text += add - limit;
        add = limit;
    }
    if (have + add > limit) {
        size_t cut = have + add - limit;
        while (cut < have && buffer[cut] != ' ') {
            cut++;
        }
        while (cut < have && ((unsigned char) buffer[cut] & 0xC0) == 0x80) {
            cut++;
        }
        memmove(buffer, buffer + cut, have - cut + 1);
        have -= cut;
    }
    while (add > 0 && ((unsigned char) *text & 0xC0) == 0x80) {
        text++;
        add--;
    }
    memcpy(buffer + have, text, add);
    buffer[have + add] = 0;
}

static void refresh_clock(void)
{
    static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
    static const char *const MONTHS[] = {"ledna",    "února", "března", "dubna", "května",    "června",
                                         "července", "srpna", "září",   "října", "listopadu", "prosince"};
    if (!net_time_valid()) {
        return;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    if (local.tm_min == last_minute) {
        return;
    }
    last_minute = local.tm_min;
    lv_label_set_text_fmt(clock_label, "%02d:%02d", local.tm_hour, local.tm_min);
    lv_label_set_text_fmt(date_label, "%s %d. %s", DAYS[local.tm_wday], local.tm_mday, MONTHS[local.tm_mon]);
}

static void on_tick(lv_timer_t *timer)
{
    static int ticks;
    if (++ticks % 20 == 0) {
        refresh_clock();
        bool active = mode != UI_IDLE || lv_display_get_inactive_time(NULL) < DIM_AFTER_MS;
        set_brightness(active ? BRIGHTNESS_ACTIVE : BRIGHTNESS_IDLE);
    }
    if (mode != UI_LISTENING && mode != UI_SPEAKING) {
        return;
    }
    float target = (mode == UI_LISTENING ? wake_microphone_level() : audio_output_level()) / 100.0f;
    level += (target - level) * 0.45f;
    int core = 100 + (int) (level * 50.0f);
    if (core == last_core) {
        return;
    }
    last_core = core;
    int middle = core + 36 + (int) (level * 34.0f);
    int outer = middle + 36 + (int) (level * 34.0f);
    lv_obj_set_size(ring_core, core, core);
    lv_obj_set_size(ring_middle, middle, middle);
    lv_obj_set_size(ring_outer, outer, outer);
}

static void on_screen_click(lv_event_t *event)
{
    app_post(APP_EVENT_TOUCH);
}

static void on_hub_click(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    app_post(APP_EVENT_PAIR_REQUEST);
}

static void build(void)
{
    lv_font_t *font_clock =
        lv_tiny_ttf_create_data(inter_clock_ttf_start, inter_clock_ttf_end - inter_clock_ttf_start, 124);
    lv_font_t *font_title = lv_tiny_ttf_create_data(inter_500_ttf_start, inter_500_ttf_end - inter_500_ttf_start, 22);
    lv_font_t *font_body = lv_tiny_ttf_create_data(inter_400_ttf_start, inter_400_ttf_end - inter_400_ttf_start, 20);
    lv_font_t *font_small = lv_tiny_ttf_create_data(inter_500_ttf_start, inter_500_ttf_end - inter_500_ttf_start, 15);
    lv_font_t *font_icon = lv_tiny_ttf_create_data(icons_ttf_start, icons_ttf_end - icons_ttf_start, 40);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, on_screen_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brand = make_label(screen, font_small, COLOR_DIM, "J.A.R.V.I.S.");
    lv_obj_set_style_text_letter_space(brand, 3, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 26, 22);

    status_dot = lv_obj_create(screen);
    lv_obj_remove_style_all(status_dot);
    lv_obj_set_size(status_dot, 10, 10);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(COLOR_FAINT), 0);
    lv_obj_align(status_dot, LV_ALIGN_TOP_RIGHT, -26, 26);

    clock_label = make_label(screen, font_clock, COLOR_TEXT, "--:--");
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 86);
    date_label = make_label(screen, font_body, COLOR_MUTED, "");
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 236);
    hint_label = make_label(screen, font_small, COLOR_FAINT, "Startuji");
    lv_obj_set_width(hint_label, 430);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 272);

    hub_button = lv_obj_create(screen);
    lv_obj_remove_style_all(hub_button);
    lv_obj_set_size(hub_button, 432, 64);
    lv_obj_align(hub_button, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(hub_button, 22, 0);
    lv_obj_set_style_bg_color(hub_button, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(hub_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub_button, 1, 0);
    lv_obj_set_style_border_color(hub_button, lv_color_hex(COLOR_LINE), 0);
    lv_obj_remove_flag(hub_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hub_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(hub_button, on_hub_click, LV_EVENT_CLICKED, NULL);
    hub_label = make_label(hub_button, font_small, COLOR_TEXT, "");
    lv_obj_center(hub_label);

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 480, 480);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(overlay, on_screen_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ring_box = lv_obj_create(overlay);
    lv_obj_remove_style_all(ring_box);
    lv_obj_set_size(ring_box, 300, 300);
    lv_obj_align(ring_box, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_remove_flag(ring_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    ring_outer = make_ring(ring_box, 180, 2, LV_OPA_30);
    ring_middle = make_ring(ring_box, 140, 3, LV_OPA_60);
    ring_core = make_ring(ring_box, 100, 0, LV_OPA_TRANSP);
    lv_obj_set_style_bg_color(ring_core, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(ring_core, lv_color_hex(COLOR_ACCENT_DEEP), 0);
    lv_obj_set_style_bg_grad_dir(ring_core, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ring_core, LV_OPA_COVER, 0);
    lv_obj_t *icon = make_label(ring_core, font_icon, 0x04121C, ICON_MICROPHONE);
    lv_obj_center(icon);

    state_label = make_label(overlay, font_small, COLOR_ACCENT, "");
    lv_obj_set_style_text_letter_space(state_label, 2, 0);
    lv_obj_align(state_label, LV_ALIGN_TOP_MID, 0, 314);

    heard_label = make_label(overlay, font_body, COLOR_MUTED, "");
    lv_obj_set_size(heard_label, 432, 26);
    lv_obj_set_style_text_align(heard_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(heard_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(heard_label, LV_ALIGN_TOP_MID, 0, 342);

    spoken_label = make_label(overlay, font_body, COLOR_TEXT, "");
    lv_obj_set_size(spoken_label, 432, 98);
    lv_obj_set_style_text_align(spoken_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(spoken_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(spoken_label, LV_ALIGN_TOP_MID, 0, 374);

    (void) font_title;
    lv_timer_create(on_tick, 50, NULL);
}

void ui_init(void)
{
    bsp_display_lock(0);
    build();
    bsp_display_unlock();
    set_brightness(BRIGHTNESS_ACTIVE);
}

void ui_set_mode(ui_mode_t next, const char *message)
{
    bsp_display_lock(0);
    mode = next;
    lv_display_trigger_activity(NULL);
    bool voice = next == UI_LISTENING || next == UI_SPEAKING || next == UI_ERROR;
    if (voice) {
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t accent = next == UI_ERROR ? COLOR_ERROR : COLOR_ACCENT;
    lv_obj_set_style_bg_color(ring_core, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_grad_color(ring_core, lv_color_hex(next == UI_ERROR ? COLOR_ERROR : COLOR_ACCENT_DEEP), 0);
    lv_obj_set_style_border_color(ring_middle, lv_color_hex(accent), 0);
    lv_obj_set_style_border_color(ring_outer, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(accent), 0);
    if (next == UI_ERROR) {
        lv_obj_set_size(ring_core, 100, 100);
        lv_obj_set_size(ring_middle, 140, 140);
        lv_obj_set_size(ring_outer, 180, 180);
        last_core = 100;
        level = 0;
        lv_label_set_text(state_label, "NĚCO SE NEPOVEDLO");
        lv_label_set_text(heard_label, "");
        lv_label_set_text(spoken_label, message ? message : "");
    } else if (next == UI_LISTENING) {
        lv_label_set_text(state_label, "POSLOUCHÁM");
    } else if (next == UI_SPEAKING) {
        lv_label_set_text(state_label, "ODPOVÍDÁM");
    }

    uint32_t dot = COLOR_FAINT;
    const char *hint = "Startuji";
    switch (next) {
    case UI_SETUP_NEEDED:
        hint = "Chybí nastavení. Otevři web flasher a nahraj Wi-Fi a klíč.";
        dot = COLOR_ERROR;
        break;
    case UI_OFFLINE:
        hint = "Připojuji se k Wi-Fi";
        break;
    case UI_IDLE:
        hint = "Řekni „Jarvis“ nebo se dotkni displeje";
        dot = COLOR_ACCENT;
        break;
    case UI_LISTENING:
    case UI_SPEAKING:
        hint = "";
        dot = COLOR_ACCENT;
        break;
    case UI_ERROR:
        hint = "";
        dot = COLOR_ERROR;
        break;
    default:
        break;
    }
    if (message && next != UI_ERROR) {
        hint = message;
    }
    lv_label_set_text(hint_label, hint);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(dot), 0);
    bsp_display_unlock();
}

void ui_set_hub(ui_hub_t hub, int devices)
{
    bsp_display_lock(0);
    if (hub == UI_HUB_HIDDEN) {
        lv_obj_add_flag(hub_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(hub_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(hub_button, lv_color_hex(hub == UI_HUB_READY ? COLOR_LINE : COLOR_ACCENT), 0);
        if (hub == UI_HUB_PAIR_OFFER) {
            lv_label_set_text(hub_label, "Našel jsem IKEA hub. Dotkni se tady pro spárování.");
        } else if (hub == UI_HUB_PAIRING) {
            lv_label_set_text(hub_label, "Stiskni tlačítko na spodku hubu DIRIGERA");
        } else {
            lv_label_set_text_fmt(hub_label, "IKEA: %d světel a zásuvek", devices);
        }
    }
    bsp_display_unlock();
}

void ui_clear_transcript(void)
{
    bsp_display_lock(0);
    heard[0] = 0;
    spoken[0] = 0;
    lv_label_set_text(heard_label, "");
    lv_label_set_text(spoken_label, "");
    bsp_display_unlock();
}

void ui_append_heard(const char *text)
{
    bsp_display_lock(0);
    append_tail(heard, HEARD_LIMIT, text);
    lv_label_set_text(heard_label, heard);
    bsp_display_unlock();
}

void ui_append_spoken(const char *text)
{
    bsp_display_lock(0);
    append_tail(spoken, SPOKEN_LIMIT, text);
    lv_label_set_text(spoken_label, spoken);
    bsp_display_unlock();
}
