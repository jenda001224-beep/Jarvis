#include "display.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#define DRAW_BUFFER_LINES 100
#define LVGL_TASK_STACK 16384

static const char *TAG = "display";
static lv_display_t *screen;
static int64_t draw_started;
static uint32_t draw_count;
static int64_t draw_total;
static int64_t draw_longest;

static void on_render(lv_event_t *event)
{
    int64_t now = esp_timer_get_time();
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) {
        draw_started = now;
        return;
    }
    if (draw_started) {
        int64_t took = now - draw_started;
        draw_count++;
        draw_total += took;
        if (took > draw_longest) {
            draw_longest = took;
        }
        draw_started = 0;
    }
}

esp_err_t display_start(uint16_t **framebuffer)
{
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_stack = LVGL_TASK_STACK;
    port.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "lvgl port");

    const bsp_display_config_t config = {0};
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &panel, &io), TAG, "panel");

    const lvgl_port_display_cfg_t display = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * DRAW_BUFFER_LINES,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_spiram = true,
                .sw_rotate = true,
            },
    };
    const lvgl_port_display_rgb_cfg_t rgb = {0};
    screen = lvgl_port_add_disp_rgb(&display, &rgb);
    ESP_RETURN_ON_FALSE(screen, ESP_FAIL, TAG, "lvgl display");
    lv_display_add_event_cb(screen, on_render, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(screen, on_render, LV_EVENT_RENDER_READY, NULL);

    esp_lcd_touch_handle_t touch = NULL;
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &touch), TAG, "touch");
    const lvgl_port_touch_cfg_t pointer = {.disp = screen, .handle = touch};
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&pointer), ESP_FAIL, TAG, "lvgl touch");

    void *pixels = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &pixels), TAG, "framebuffer");
    *framebuffer = pixels;
    return ESP_OK;
}

void display_set_orientation(int quarter)
{
    if (screen) {
        lv_display_set_rotation(screen, (lv_display_rotation_t) (quarter & 3));
    }
}

void display_take_stats(int *draws, int *average_microseconds, int *longest_microseconds)
{
    *draws = (int) draw_count;
    *average_microseconds = draw_count ? (int) (draw_total / draw_count) : 0;
    *longest_microseconds = (int) draw_longest;
    draw_count = 0;
    draw_total = 0;
    draw_longest = 0;
}
