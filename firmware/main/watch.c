#include "watch.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "agenda.h"
#include "bambu.h"
#include "calendar.h"
#include "cloud.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "memory.h"
#include "printer.h"
#include "travel.h"
#include "weather.h"

#define INTERVAL_MS 20000
#define VALID_TIME 1600000000
#define LOOK_AHEAD_SECONDS (3 * 60 * 60)
#define UPCOMING 8
#define PLANNED 16
#define BEFORE_WITHOUT_PLACE_MINUTES 15
#define RAIN_AHEAD_SECONDS (45 * 60)
#define RAIN_REPEAT_SECONDS (6 * 60 * 60)

typedef struct {
    uint32_t key;
    time_t start;
    time_t leave_at;
    bool told;
    char text[NOTICE_TEXT];
} departure_t;

static SemaphoreHandle_t lock;
static volatile int mask = WATCH_PRINTER | WATCH_DEPARTURE | WATCH_RAIN;
static volatile bool away;
static EXTERNAL_RAM departure_t planned[PLANNED];
static char printer_before[12];
static time_t rain_told;

static const char *TAG = "watch";

uint32_t watch_notice(const char *source, const char *text, bool speak)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t before = notices_unread();
    uint32_t id = notices_add((int64_t) time(NULL), source, text, speak);
    bool fresh = notices_unread() != before;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "notice from %s: %s", source, text);
    if (fresh && away) {
        cloud_push(text);
    }
    return id;
}

int watch_notices(notice_t *out, int capacity)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    int count = notices_list(out, capacity);
    xSemaphoreGive(lock);
    return count;
}

int watch_unread(void)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    int count = notices_unread();
    xSemaphoreGive(lock);
    return count;
}

size_t watch_peek_spoken(char *out, size_t capacity)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = notices_join(out, capacity, true, false);
    xSemaphoreGive(lock);
    return length;
}

size_t watch_take_spoken(char *out, size_t capacity)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = notices_join(out, capacity, true, true);
    xSemaphoreGive(lock);
    return length;
}

size_t watch_describe_unread(char *out, size_t capacity)
{
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = notices_join(out, capacity, false, false);
    xSemaphoreGive(lock);
    return length;
}

void watch_mark_read(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    notices_mark_all();
    xSemaphoreGive(lock);
}

void watch_mark_one(uint32_t id)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    notices_mark(id);
    xSemaphoreGive(lock);
}

void watch_silence(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    notices_silence();
    xSemaphoreGive(lock);
}

void watch_set_away(bool value)
{
    away = value;
}

bool watch_away(void)
{
    return away;
}

void watch_set_mask(int value)
{
    mask = value;
}

static void check_printer(void)
{
    static EXTERNAL_RAM bambu_status_t status;
    if (!printer_configured() || !printer_status(&status)) {
        printer_before[0] = 0;
        return;
    }
    bambu_event_t event = bambu_event(printer_before, status.state);
    strlcpy(printer_before, status.state, sizeof(printer_before));
    if (event == BAMBU_EVENT_NONE || !(mask & WATCH_PRINTER)) {
        return;
    }
    char text[NOTICE_TEXT];
    const char *job = status.job[0] ? status.job : "na tiskárně";
    snprintf(text, sizeof(text),
             event == BAMBU_EVENT_FINISHED  ? "Tisk %s je hotový."
             : event == BAMBU_EVENT_FAILED ? "Tisk %s selhal."
                                           : "Tisk %s se zastavil.",
             job);
    watch_notice("tiskárna", text, true);
}

static uint32_t event_key(const agenda_event_t *event)
{
    uint32_t hash = 2166136261U ^ (uint32_t) event->start;
    for (const char *c = event->title; *c; c++) {
        hash = (hash ^ (unsigned char) *c) * 16777619U;
    }
    return hash | 1;
}

static departure_t *plan_slot(time_t now, uint32_t key, bool *fresh)
{
    departure_t *free_slot = NULL;
    *fresh = false;
    for (int i = 0; i < PLANNED; i++) {
        if (planned[i].key == key) {
            return &planned[i];
        }
        if (!free_slot && (planned[i].key == 0 || planned[i].start < now)) {
            free_slot = &planned[i];
        }
    }
    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->key = key;
        *fresh = true;
    }
    return free_slot;
}

static void plan_departure(departure_t *plan, const agenda_event_t *event)
{
    struct tm local;
    localtime_r(&event->start, &local);
    plan->start = event->start;
    int minutes = 0;
    if (event->place[0] && travel_configured() && travel_minutes(event->place, &minutes)) {
        plan->leave_at = event->start - (minutes + RESERVE_MINUTES) * 60;
        snprintf(plan->text, sizeof(plan->text), "Je čas vyrazit: %.72s v %d:%02d, cesta trvá asi %d minut.", event->title,
                 local.tm_hour % 24, local.tm_min % 60, minutes % 10000);
    } else {
        plan->leave_at = event->start - BEFORE_WITHOUT_PLACE_MINUTES * 60;
        snprintf(plan->text, sizeof(plan->text), "Za chvíli začíná %.72s, v %d:%02d.", event->title, local.tm_hour % 24, local.tm_min % 60);
    }
}

static void check_departures(time_t now)
{
    static EXTERNAL_RAM agenda_event_t events[UPCOMING];
    if (!calendar_configured()) {
        return;
    }
    int count = calendar_between(now, now + LOOK_AHEAD_SECONDS, events, UPCOMING);
    for (int i = 0; i < count; i++) {
        if (events[i].all_day || events[i].start <= now) {
            continue;
        }
        bool fresh;
        departure_t *plan = plan_slot(now, event_key(&events[i]), &fresh);
        if (!plan) {
            continue;
        }
        if (fresh) {
            plan_departure(plan, &events[i]);
        }
        if (!plan->told && now >= plan->leave_at) {
            plan->told = true;
            if (mask & WATCH_DEPARTURE) {
                watch_notice("kalendář", plan->text, true);
            }
        }
    }
}

static void check_rain(time_t now)
{
    time_t rain_at;
    if (!(mask & WATCH_RAIN) || !weather_rain_at(&rain_at)) {
        return;
    }
    if (rain_at <= now || rain_at - now > RAIN_AHEAD_SECONDS || now - rain_told < RAIN_REPEAT_SECONDS) {
        return;
    }
    rain_told = now;
    char text[NOTICE_TEXT];
    int minutes = (int) ((rain_at - now + 59) / 60);
    snprintf(text, sizeof(text), "Asi za %d minut začne pršet.", minutes % 1000);
    watch_notice("počasí", text, true);
}

static void watch_task(void *arg)
{
    (void) arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
        time_t now = time(NULL);
        check_printer();
        if (now > VALID_TIME) {
            check_departures(now);
            check_rain(now);
        }
    }
}

void watch_start(void)
{
    lock = xSemaphoreCreateMutex();
    if (lock) {
        xTaskCreatePinnedToCoreWithCaps(watch_task, "watch", 16384, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
    }
}
