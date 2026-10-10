#include "display.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lvgl_port.h"

#define DRAW_BUFFER_LINES 100
#define LVGL_TASK_STACK 16384

static const char *TAG = "display";

esp_err_t display_start(uint16_t **framebuffer)
{
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_stack = LVGL_TASK_STACK;
    port.task_stack_caps = MALLOC_CAP_SPIRAM;
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
            },
    };
    const lvgl_port_display_rgb_cfg_t rgb = {0};
    lv_display_t *screen = lvgl_port_add_disp_rgb(&display, &rgb);
    ESP_RETURN_ON_FALSE(screen, ESP_FAIL, TAG, "lvgl display");

    esp_lcd_touch_handle_t touch = NULL;
    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &touch), TAG, "touch");
    const lvgl_port_touch_cfg_t pointer = {.disp = screen, .handle = touch};
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&pointer), ESP_FAIL, TAG, "lvgl touch");

    void *pixels = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &pixels), TAG, "framebuffer");
    *framebuffer = pixels;
    return ESP_OK;
}
