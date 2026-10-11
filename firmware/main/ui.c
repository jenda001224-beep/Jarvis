#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "fx.h"
#include "lvgl.h"
#include "net.h"
#include "scene.h"
#include "wake.h"

#define COLOR_ACCENT 0x5BD7FF
#define COLOR_ERROR 0xFF5C4A
#define COLOR_TEXT 0xF5F9FF
#define COLOR_MUTED 0x8DA2B5
#define COLOR_DIM 0x51606E
#define COLOR_PILL 0x0B1219

#define HEARD_LIMIT 46
#define SPOKEN_LIMIT 68
#define BRIGHTNESS_ACTIVE 100
#define BRIGHTNESS_IDLE 45
#define DIM_AFTER_MS 60000
#define FRAME_MS 33
#define REST_MS 5
#define RENDER_CORE 1
#define VOICE_TEXT_DELAY_MS 420
#define TICK_MS 50

extern const uint8_t inter_400_ttf_start[] asm("_binary_inter_400_ttf_start");
extern const uint8_t inter_400_ttf_end[] asm("_binary_inter_400_ttf_end");
extern const uint8_t inter_500_ttf_start[] asm("_binary_inter_500_ttf_start");
extern const uint8_t inter_500_ttf_end[] asm("_binary_inter_500_ttf_end");
extern const uint8_t inter_clock_ttf_start[] asm("_binary_inter_clock_ttf_start");
extern const uint8_t inter_clock_ttf_end[] asm("_binary_inter_clock_ttf_end");

static uint16_t *pixels;
static lv_obj_t *clock_label;
static lv_obj_t *date_label;
static lv_obj_t *brand_label;
static lv_obj_t *hint_label;
static lv_obj_t *status_label;
static lv_obj_t *hub_pill;
static lv_obj_t *hub_label;
static lv_obj_t *caption_label;
static lv_obj_t *heard_label;
static lv_obj_t *spoken_label;
static lv_obj_t *lamp_cover;

static ui_mode_t mode = UI_STARTING;
static ui_hub_t hub_state = UI_HUB_HIDDEN;
static char heard[HEARD_LIMIT + 8];
static char spoken[SPOKEN_LIMIT + 8];
static char hint[64];
static char notice[64];
static int last_minute = -1;
static int brightness = -1;
static bool idle_shown = true;
static bool voice_shown;
static bool returning;
static uint32_t voice_text_at;
static volatile bool lamp;
static volatile int stat_frames;
static volatile int stat_microseconds;

static bool is_voice(ui_mode_t value)
{
    return value == UI_LISTENING || value == UI_THINKING || value == UI_SPEAKING || value == UI_ERROR;
}

static void set_visible(lv_obj_t *object, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_idle(bool visible)
{
    idle_shown = visible;
    set_visible(clock_label, visible);
    set_visible(date_label, visible);
    set_visible(brand_label, visible);
    set_visible(status_label, visible);
    bool pill = visible && (hub_state == UI_HUB_PAIR_OFFER || hub_state == UI_HUB_PAIRING);
    set_visible(hub_pill, pill);
    set_visible(hint_label, visible && !pill);
}

static void show_voice(bool visible)
{
    voice_shown = visible;
    set_visible(caption_label, visible);
    set_visible(heard_label, visible);
    set_visible(spoken_label, visible);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int spacing, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, spacing, 0);
    lv_label_set_text(label, text);
    return label;
}

static void apply_hint(void)
{
    bool noticed = notice[0] && mode == UI_IDLE;
    lv_obj_set_style_text_color(hint_label, lv_color_hex(noticed ? COLOR_ACCENT : COLOR_DIM), 0);
    lv_label_set_text(hint_label, noticed ? notice : hint);
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
        while (cut < have && (buffer[cut] == ' ' || ((unsigned char) buffer[cut] & 0xC0) == 0x80)) {
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
    static const char *const DAYS[] = {"NEDĚLE", "PONDĚLÍ", "ÚTERÝ", "STŘEDA", "ČTVRTEK", "PÁTEK", "SOBOTA"};
    static const char *const MONTHS[] = {"LEDNA",    "ÚNORA", "BŘEZNA", "DUBNA", "KVĚTNA",    "ČERVNA",
                                         "ČERVENCE", "SRPNA", "ZÁŘÍ",   "ŘÍJNA", "LISTOPADU", "PROSINCE"};
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
    if (++ticks % (1000 / TICK_MS) == 0) {
        refresh_clock();
        bool active = lamp || mode != UI_IDLE || lv_display_get_inactive_time(NULL) < DIM_AFTER_MS;
        set_brightness(active ? BRIGHTNESS_ACTIVE : BRIGHTNESS_IDLE);
    }
    if (voice_text_at && is_voice(mode) && lv_tick_get() >= voice_text_at) {
        voice_text_at = 0;
        show_voice(true);
    }
    if (returning && !is_voice(mode) && scene_settled()) {
        returning = false;
        scene_set_hole(true);
        show_idle(true);
    }
}

static void on_screen_click(lv_event_t *event)
{
    app_post(APP_EVENT_TOUCH);
}

static void on_hub_click(lv_event_t *event)
{
    app_post(APP_EVENT_PAIR_REQUEST);
}

static void render_task(void *arg)
{
    fx_frame_t frame = {0};
    int64_t last = esp_timer_get_time();
    int64_t window = last;
    int frames = 0;
    int64_t spent = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        float seconds = (now - last) / 1000000.0f;
        last = now;
        if (net_time_valid()) {
            struct timeval moment;
            gettimeofday(&moment, NULL);
            scene_set_minute(((moment.tv_sec % 60) + moment.tv_usec / 1000000.0f) / 60.0f);
        }
        scene_set_levels(wake_microphone_level() / 100.0f, audio_output_level() / 100.0f);
        scene_step(seconds, &frame);
        if (!lamp) {
            fx_render(&frame, pixels);
        }
        int64_t took = esp_timer_get_time() - now;
        spent += took;
        frames++;
        if (now - window >= 3000000) {
            stat_frames = (int) (frames * 1000000LL / (now - window));
            stat_microseconds = (int) (spent / frames);
            window = now;
            frames = 0;
            spent = 0;
        }
        int rest = FRAME_MS - (int) (took / 1000);
        vTaskDelay(pdMS_TO_TICKS(rest < REST_MS ? REST_MS : rest));
    }
}

static void *allocate_external(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static void build(void)
{
    lv_font_t *font_clock =
        lv_tiny_ttf_create_data(inter_clock_ttf_start, inter_clock_ttf_end - inter_clock_ttf_start, 92);
    lv_font_t *font_caption = lv_tiny_ttf_create_data(inter_500_ttf_start, inter_500_ttf_end - inter_500_ttf_start, 12);
    lv_font_t *font_small = lv_tiny_ttf_create_data(inter_500_ttf_start, inter_500_ttf_end - inter_500_ttf_start, 13);
    lv_font_t *font_heard = lv_tiny_ttf_create_data(inter_400_ttf_start, inter_400_ttf_end - inter_400_ttf_start, 17);
    lv_font_t *font_body = lv_tiny_ttf_create_data(inter_400_ttf_start, inter_400_ttf_end - inter_400_ttf_start, 20);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, on_screen_click, LV_EVENT_CLICKED, NULL);

    brand_label = make_label(screen, font_caption, COLOR_DIM, 3, "J.A.R.V.I.S.");
    lv_obj_align(brand_label, LV_ALIGN_TOP_LEFT, 24, 18);
    status_label = make_label(screen, font_caption, COLOR_MUTED, 2, "");
    lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, -24, 18);

    clock_label = make_label(screen, font_clock, COLOR_TEXT, 0, "--:--");
    lv_obj_align(clock_label, LV_ALIGN_TOP_MID, 0, 150);
    date_label = make_label(screen, font_small, COLOR_MUTED, 3, "");
    lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 264);

    hint_label = make_label(screen, font_caption, COLOR_DIM, 3, "STARTUJI");
    lv_obj_set_width(hint_label, 440);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 442);

    hub_pill = lv_obj_create(screen);
    lv_obj_remove_style_all(hub_pill);
    lv_obj_set_size(hub_pill, 340, 38);
    lv_obj_align(hub_pill, LV_ALIGN_TOP_MID, 0, 430);
    lv_obj_set_style_radius(hub_pill, 19, 0);
    lv_obj_set_style_bg_color(hub_pill, lv_color_hex(COLOR_PILL), 0);
    lv_obj_set_style_bg_opa(hub_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub_pill, 1, 0);
    lv_obj_set_style_border_color(hub_pill, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_remove_flag(hub_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hub_pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(hub_pill, on_hub_click, LV_EVENT_CLICKED, NULL);
    hub_label = make_label(hub_pill, font_caption, COLOR_TEXT, 2, "");
    lv_obj_center(hub_label);

    caption_label = make_label(screen, font_caption, COLOR_ACCENT, 4, "");
    lv_obj_set_width(caption_label, 400);
    lv_obj_set_style_text_align(caption_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(caption_label, LV_ALIGN_TOP_MID, 0, 350);

    heard_label = make_label(screen, font_heard, COLOR_MUTED, 0, "");
    lv_obj_set_size(heard_label, 400, 24);
    lv_obj_set_style_text_align(heard_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(heard_label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align(heard_label, LV_ALIGN_TOP_MID, 0, 368);

    spoken_label = make_label(screen, font_body, COLOR_TEXT, 0, "");
    lv_obj_set_size(spoken_label, 400, 50);
    lv_obj_set_style_text_align(spoken_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(spoken_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(spoken_label, LV_ALIGN_TOP_MID, 0, 394);

    lamp_cover = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(lamp_cover);
    lv_obj_set_size(lamp_cover, FX_WIDTH, FX_HEIGHT);
    lv_obj_set_style_bg_color(lamp_cover, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(lamp_cover, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lamp_cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(lamp_cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(lamp_cover, on_screen_click, LV_EVENT_CLICKED, NULL);

    show_voice(false);
    show_idle(true);
    lv_timer_create(on_tick, TICK_MS, NULL);
}

void ui_init(uint16_t *framebuffer)
{
    pixels = framebuffer;
    scene_init();
    scene_set_hole(true);
    bsp_display_lock(0);
    build();
    lv_refr_now(NULL);
    bsp_display_unlock();
    set_brightness(BRIGHTNESS_ACTIVE);
    if (pixels && fx_init(allocate_external)) {
        xTaskCreatePinnedToCoreWithCaps(render_task, "render", 6144, NULL, 3, NULL, RENDER_CORE, MALLOC_CAP_SPIRAM);
    }
}

void ui_set_mode(ui_mode_t next, const char *message)
{
    static const scene_mode_t SCENES[] = {
        [UI_STARTING] = SCENE_IDLE, [UI_SETUP_NEEDED] = SCENE_IDLE, [UI_OFFLINE] = SCENE_IDLE,
        [UI_IDLE] = SCENE_IDLE,     [UI_LISTENING] = SCENE_LISTENING, [UI_THINKING] = SCENE_THINKING,
        [UI_SPEAKING] = SCENE_SPEAKING, [UI_ERROR] = SCENE_ERROR,
    };
    bsp_display_lock(0);
    bool was_voice = is_voice(mode);
    bool now_voice = is_voice(next);
    mode = next;
    lv_display_trigger_activity(NULL);

    if (now_voice) {
        uint32_t color = next == UI_ERROR ? COLOR_ERROR : COLOR_ACCENT;
        lv_obj_set_style_text_color(caption_label, lv_color_hex(color), 0);
        const char *caption = next == UI_LISTENING  ? "POSLOUCHÁM"
                              : next == UI_THINKING ? "PŘEMÝŠLÍM"
                              : next == UI_SPEAKING ? "JARVIS"
                                                    : "NĚCO SE NEPOVEDLO";
        lv_label_set_text(caption_label, caption);
        if (next == UI_ERROR) {
            lv_label_set_text(heard_label, "");
            lv_label_set_text(spoken_label, message ? message : "");
        }
    } else {
        const char *usual = next == UI_SETUP_NEEDED ? "CHYBÍ NASTAVENÍ · OTEVŘI FLASHER"
                            : next == UI_OFFLINE    ? "PŘIPOJUJI WI-FI"
                            : next == UI_IDLE       ? "ŘEKNI „JARVISI“"
                                                    : "STARTUJI";
        strlcpy(hint, message ? message : usual, sizeof(hint));
        apply_hint();
    }

    if (now_voice && !was_voice) {
        returning = false;
        show_idle(false);
        lv_refr_now(NULL);
        scene_set_hole(false);
        voice_text_at = lv_tick_get() + VOICE_TEXT_DELAY_MS;
    } else if (!now_voice && was_voice) {
        voice_text_at = 0;
        show_voice(false);
        lv_refr_now(NULL);
        returning = true;
    }
    scene_set_mode(SCENES[next]);
    bsp_display_unlock();
}

void ui_set_hub(ui_hub_t hub, int devices)
{
    bsp_display_lock(0);
    hub_state = hub;
    if (hub == UI_HUB_PAIR_OFFER) {
        lv_label_set_text(hub_label, "DOTKNI SE PRO SPÁROVÁNÍ S IKEA");
    } else if (hub == UI_HUB_PAIRING) {
        lv_label_set_text(hub_label, "STISKNI TLAČÍTKO NA SPODKU HUBU");
    }
    if (idle_shown) {
        show_idle(true);
    }
    bsp_display_unlock();
}

void ui_set_notice(const char *text)
{
    bsp_display_lock(0);
    strlcpy(notice, text ? text : "", sizeof(notice));
    apply_hint();
    bsp_display_unlock();
}

void ui_set_status(const char *text)
{
    bsp_display_lock(0);
    lv_label_set_text(status_label, text ? text : "");
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

void ui_set_lamp(bool on)
{
    bsp_display_lock(0);
    lamp = on;
    set_visible(lamp_cover, on);
    lv_display_trigger_activity(NULL);
    if (on) {
        set_brightness(BRIGHTNESS_ACTIVE);
    }
    lv_refr_now(NULL);
    bsp_display_unlock();
}

bool ui_lamp(void)
{
    return lamp;
}

void ui_render_stats(int *frames_per_second, int *microseconds_per_frame)
{
    *frames_per_second = stat_frames;
    *microseconds_per_frame = stat_microseconds;
}
