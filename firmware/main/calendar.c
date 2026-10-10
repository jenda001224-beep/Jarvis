#include "calendar.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "agenda.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "settings.h"
#include "web.h"

#define MAX_EVENTS 96
#define WINDOW_DAYS 14
#define REFRESH_MS (10 * 60 * 1000)
#define RETRY_MS (60 * 1000)

static const char *TAG = "calendar";

static agenda_event_t *events;
static agenda_event_t *fresh;
static agenda_parser_t *parser;
static int event_count;
static time_t window_start;
static time_t window_end;
static bool loaded;
static SemaphoreHandle_t lock;

static time_t day_start(time_t moment, int offset_days)
{
    struct tm local;
    localtime_r(&moment, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_mday += offset_days;
    local.tm_isdst = -1;
    return mktime(&local);
}

static char *failure(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

bool calendar_configured(void)
{
    return settings_get()->calendars[0][0] != 0;
}

static bool feed(void *context, const char *data, size_t length)
{
    agenda_parser_feed(context, data, length);
    return true;
}

static bool refresh(void)
{
    const settings_t *settings = settings_get();
    time_t now = time(NULL);
    time_t from = day_start(now, 0);
    time_t to = day_start(now, WINDOW_DAYS);
    agenda_parser_begin(parser, from, to);
    bool complete = true;
    for (int i = 0; i < SETTINGS_CALENDARS && settings->calendars[i][0]; i++) {
        char url[sizeof(settings->calendars[0]) + 8];
        const char *address = settings->calendars[i];
        if (strncasecmp(address, "webcal://", 9) == 0) {
            snprintf(url, sizeof(url), "https://%s", address + 9);
        } else {
            strlcpy(url, address, sizeof(url));
        }
        if (!web_stream(url, feed, parser)) {
            ESP_LOGW(TAG, "calendar %d could not be read", i + 1);
            complete = false;
        }
        agenda_parser_source_end(parser);
    }
    int count = agenda_parser_finish(parser, fresh, MAX_EVENTS);
    if (!complete && (loaded || count == 0)) {
        return false;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(events, fresh, (size_t) count * sizeof(agenda_event_t));
    event_count = count;
    window_start = from;
    window_end = to;
    loaded = true;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "%d events in the next %d days", count, WINDOW_DAYS);
    return complete;
}

static void calendar_task(void *arg)
{
    for (;;) {
        if (!net_connected() || !net_time_valid()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        bool complete = refresh();
        vTaskDelay(pdMS_TO_TICKS(complete ? REFRESH_MS : RETRY_MS));
    }
}

void calendar_start(void)
{
    if (!calendar_configured()) {
        return;
    }
    lock = xSemaphoreCreateMutex();
    events = heap_caps_calloc(MAX_EVENTS, sizeof(agenda_event_t), MALLOC_CAP_SPIRAM);
    fresh = heap_caps_calloc(MAX_EVENTS, sizeof(agenda_event_t), MALLOC_CAP_SPIRAM);
    parser = heap_caps_malloc(agenda_parser_size(), MALLOC_CAP_SPIRAM);
    if (!lock || !events || !fresh || !parser) {
        ESP_LOGE(TAG, "not enough memory");
        return;
    }
    xTaskCreatePinnedToCoreWithCaps(calendar_task, "calendar", 12288, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
}

char *calendar_tool_get(const char *args_json)
{
    if (!lock || !events) {
        return failure("Kalendář není nastavený.");
    }
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(args, "date");
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(args, "days");
    time_t from = day_start(time(NULL), 0);
    int year;
    int month;
    int day;
    if (cJSON_IsString(date) && sscanf(date->valuestring, "%d-%d-%d", &year, &month, &day) == 3 && year > 2000 &&
        year < 2200 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        struct tm local = {.tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_isdst = -1};
        from = mktime(&local);
    }
    int span = cJSON_IsNumber(days) ? days->valueint : 1;
    span = span < 1 ? 1 : span > WINDOW_DAYS ? WINDOW_DAYS : span;
    time_t to = day_start(from, span);
    cJSON_Delete(args);

    char *result;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!loaded) {
        result = failure("Kalendář se zatím nepodařilo načíst.");
    } else if (from < window_start || from >= window_end) {
        result = failure("Kalendář znám jen od dneška na čtrnáct dní dopředu.");
    } else {
        result = agenda_describe(events, event_count, from, to < window_end ? to : window_end);
    }
    xSemaphoreGive(lock);
    return result;
}

size_t calendar_today(char *out, size_t capacity)
{
    out[0] = 0;
    if (!lock || !events) {
        return 0;
    }
    time_t now = time(NULL);
    size_t length = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (loaded) {
        length = agenda_summary(events, event_count, day_start(now, 0), day_start(now, 1), out, capacity);
    }
    xSemaphoreGive(lock);
    return length;
}
