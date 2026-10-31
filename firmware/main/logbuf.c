#include "logbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define RING_SIZE (128 * 1024)
#define LINE_SIZE 320

static char *ring;
static uint32_t total;
static SemaphoreHandle_t lock;
static vprintf_like_t previous;
static char line[LINE_SIZE];

static void store(const char *text, size_t length)
{
    size_t at = total % RING_SIZE;
    size_t first = length < RING_SIZE - at ? length : RING_SIZE - at;
    memcpy(ring + at, text, first);
    memcpy(ring, text + first, length - first);
    total += (uint32_t) length;
}

static int on_log(const char *format, va_list arguments)
{
    if (!xPortInIsrContext() && xSemaphoreTake(lock, 0) == pdTRUE) {
        va_list copy;
        va_copy(copy, arguments);
        int size = vsnprintf(line, sizeof(line), format, copy);
        va_end(copy);
        if (size > 0) {
            store(line, (size_t) size < sizeof(line) ? (size_t) size : sizeof(line) - 1);
        }
        xSemaphoreGive(lock);
    }
    return previous(format, arguments);
}

void logbuf_start(void)
{
    ring = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    lock = xSemaphoreCreateMutex();
    if (ring && lock) {
        previous = esp_log_set_vprintf(on_log);
    }
}

size_t logbuf_read(uint32_t after, char *out, size_t capacity, uint32_t *next)
{
    if (!lock || !ring || capacity == 0) {
        *next = after;
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t oldest = total > RING_SIZE ? total - RING_SIZE : 0;
    uint32_t from = after < oldest ? oldest : after > total ? total : after;
    size_t length = total - from;
    if (length > capacity - 1) {
        length = capacity - 1;
    }
    for (size_t i = 0; i < length; i++) {
        out[i] = ring[(from + i) % RING_SIZE];
    }
    out[length] = 0;
    *next = from + (uint32_t) length;
    xSemaphoreGive(lock);
    return length;
}
