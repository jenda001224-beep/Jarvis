#include "console.h"

#include <string.h>

#include "app.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

#define POLL_MS 30

static SemaphoreHandle_t lock;
static char pending[CONSOLE_LINE];
static bool waiting;

static void accept(char *line, size_t *length, const uint8_t *bytes, int count)
{
    for (int i = 0; i < count; i++) {
        if (bytes[i] != '\r' && bytes[i] != '\n') {
            if (*length < CONSOLE_LINE - 1) {
                line[(*length)++] = (char) bytes[i];
            }
            continue;
        }
        if (*length == 0) {
            continue;
        }
        line[*length] = 0;
        *length = 0;
        xSemaphoreTake(lock, portMAX_DELAY);
        strcpy(pending, line);
        waiting = true;
        xSemaphoreGive(lock);
        app_post(APP_EVENT_CONSOLE);
    }
}

static void console_task(void *arg)
{
    static char wired[CONSOLE_LINE];
    static char native[CONSOLE_LINE];
    size_t wired_length = 0;
    size_t native_length = 0;
    uint8_t bytes[64];
    for (;;) {
        int count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), pdMS_TO_TICKS(POLL_MS));
        accept(wired, &wired_length, bytes, count > 0 ? count : 0);
        if (usb_serial_jtag_ll_rxfifo_data_available()) {
            count = usb_serial_jtag_ll_read_rxfifo(bytes, sizeof(bytes));
            accept(native, &native_length, bytes, count);
        }
    }
}

void console_start(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock || uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0) != ESP_OK) {
        return;
    }
    xTaskCreatePinnedToCore(console_task, "console", 3072, NULL, 2, NULL, 0);
}

size_t console_take(char *out, size_t capacity)
{
    size_t length = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (waiting) {
        strlcpy(out, pending, capacity);
        length = strlen(out);
        waiting = false;
    }
    xSemaphoreGive(lock);
    return length;
}
