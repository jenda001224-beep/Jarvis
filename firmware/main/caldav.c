#include "caldav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calendar.h"
#include "dav.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "memory.h"
#include "settings.h"
#include "textfold.h"
#include "web.h"

#define SERVER "https://caldav.icloud.com"
#define RESPONSE_LIMIT (64 * 1024)
#define MAX_CALENDARS 12
#define DEFAULT_MINUTES 60
#define STATUS_MULTI 207
#define STATUS_UNAUTHORIZED 401

static const char *TAG = "caldav";

static EXTERNAL_RAM dav_calendar_t calendars[MAX_CALENDARS];
static int calendar_count;
static bool discovered;
static char origin[96];
static char last_error[96];
static SemaphoreHandle_t lock;

static const char *server(void)
{
    const char *custom = settings_get()->apple_server;
    return custom[0] ? custom : SERVER;
}

bool caldav_configured(void)
{
    const settings_t *settings = settings_get();
    return settings->apple_id[0] && settings->apple_password[0];
}

static void authorization(char *out, size_t capacity)
{
    const settings_t *settings = settings_get();
    char plain[160];
    int size = snprintf(plain, sizeof(plain), "%s:%s", settings->apple_id, settings->apple_password);
    memcpy(out, "Basic ", 6);
    size_t written = 0;
    mbedtls_base64_encode((unsigned char *) out + 6, capacity - 7, &written, (const unsigned char *) plain, (size_t) size);
    out[6 + written] = 0;
}

static char *dav_call(web_method_t method, const char *url, const char *depth, const char *body, const char *type, int *status,
                      size_t *length, web_sink_t sink, void *context)
{
    char auth[256];
    authorization(auth, sizeof(auth));
    web_header_t headers[4] = {{"Authorization", auth}, {"Content-Type", type}};
    int count = 2;
    if (depth) {
        headers[count++] = (web_header_t) {"Depth", depth};
    }
    const web_request_t request = {
        .url = url,
        .method = method,
        .headers = headers,
        .header_count = count,
        .body = body,
        .limit = RESPONSE_LIMIT,
        .sink = sink,
        .context = context,
        .timeout_ms = 20000,
    };
    return web_request(&request, status, length);
}

static bool discover(void)
{
    if (discovered) {
        return true;
    }
    char url[DAV_HREF + 96];
    char href[DAV_HREF];
    int status = 0;
    size_t length = 0;
    snprintf(url, sizeof(url), "%s/", server());
    char *body = dav_call(WEB_PROPFIND, url, "0", DAV_PRINCIPAL_BODY, "application/xml; charset=utf-8", &status, &length, NULL, NULL);
    if (status == STATUS_UNAUTHORIZED) {
        strlcpy(last_error, "Apple odmítl přihlášení, zkontroluj heslo pro aplikaci.", sizeof(last_error));
        free(body);
        return false;
    }
    bool found = body && status == STATUS_MULTI && dav_find_href(body, length, "current-user-principal", href, sizeof(href));
    free(body);
    if (!found) {
        ESP_LOGW(TAG, "no principal, status %d", status);
        snprintf(last_error, sizeof(last_error), "Kalendář neodpovídá (%d).", status);
        return false;
    }
    char base[sizeof(origin)];
    dav_origin(url, base, sizeof(base));
    dav_join_url(base, href, url, sizeof(url));
    body = dav_call(WEB_PROPFIND, url, "0", DAV_HOME_BODY, "application/xml; charset=utf-8", &status, &length, NULL, NULL);
    found = body && status == STATUS_MULTI && dav_find_href(body, length, "calendar-home-set", href, sizeof(href));
    free(body);
    if (!found) {
        ESP_LOGW(TAG, "no calendar home, status %d", status);
        snprintf(last_error, sizeof(last_error), "Kalendáře se nenašly (%d).", status);
        return false;
    }
    dav_join_url(base, href, url, sizeof(url));
    dav_origin(url, origin, sizeof(origin));
    body = dav_call(WEB_PROPFIND, url, "1", DAV_CALENDARS_BODY, "application/xml; charset=utf-8", &status, &length, NULL, NULL);
    int count = body && status == STATUS_MULTI ? dav_parse_calendars(body, length, calendars, MAX_CALENDARS) : 0;
    free(body);
    if (count == 0) {
        ESP_LOGW(TAG, "no calendars, status %d", status);
        snprintf(last_error, sizeof(last_error), "Žádný kalendář se nenašel (%d).", status);
        return false;
    }
    calendar_count = count;
    discovered = true;
    last_error[0] = 0;
    ESP_LOGI(TAG, "%d calendars", count);
    return true;
}

typedef struct {
    agenda_parser_t *parser;
    dav_stream_t stream;
} reading_t;

static void on_data(void *context, const char *text, size_t length)
{
    agenda_parser_feed(context, text, length);
}

static void on_data_end(void *context)
{
    agenda_parser_source_end(context);
}

static bool on_stream(void *context, const char *data, size_t length)
{
    dav_stream_feed(context, data, length);
    return true;
}

static void take_lock(void)
{
    if (!lock) {
        lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(lock, portMAX_DELAY);
}

bool caldav_read(time_t from, time_t to, agenda_parser_t *parser)
{
    take_lock();
    bool done = discover();
    if (done) {
        char *query = dav_query_body(from, to);
        static EXTERNAL_RAM dav_stream_t stream;
        for (int i = 0; query && i < calendar_count; i++) {
            char url[DAV_HREF + 96];
            dav_join_url(origin, calendars[i].href, url, sizeof(url));
            int status = 0;
            dav_stream_begin(&stream, on_data, on_data_end, parser);
            free(dav_call(WEB_REPORT, url, "1", query, "application/xml; charset=utf-8", &status, NULL, on_stream, &stream));
            dav_stream_finish(&stream);
            agenda_parser_source_end(parser);
            if (status != STATUS_MULTI) {
                ESP_LOGW(TAG, "%s answered %d", calendars[i].name, status);
                done = false;
            }
        }
        free(query);
    }
    xSemaphoreGive(lock);
    return done;
}

static char *reply(const char *key, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key, message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static bool parse_moment(const char *text, time_t *moment, bool *all_day)
{
    int year;
    int month;
    int day;
    int hour = 0;
    int minute = 0;
    int fields = sscanf(text, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute);
    if (fields != 3 && fields != 5) {
        fields = sscanf(text, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute);
    }
    if ((fields != 3 && fields != 5) || year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59) {
        return false;
    }
    struct tm local = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_isdst = -1,
    };
    *moment = mktime(&local);
    *all_day = fields == 3;
    return *moment != (time_t) -1;
}

static int choose_calendar(const char *wanted)
{
    char folded[DAV_NAME];
    char candidate[DAV_NAME];
    const char *name = wanted && wanted[0] ? wanted : settings_get()->apple_calendar;
    text_fold(name, folded, sizeof(folded));
    for (int i = 0; folded[0] && i < calendar_count; i++) {
        text_fold(calendars[i].name, candidate, sizeof(candidate));
        if (strcmp(candidate, folded) == 0) {
            return i;
        }
    }
    return 0;
}

static const char *argument(const cJSON *args, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(args, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

char *caldav_tool_add(const char *args_json)
{
    if (!caldav_configured()) {
        return reply("error", "Zápis do kalendáře není nastavený. Pán má v aplikaci Jarvis zadat Apple účet.");
    }
    cJSON *args = cJSON_Parse(args_json);
    const char *title = argument(args, "title");
    const char *start_text = argument(args, "start");
    time_t start = 0;
    bool all_day = false;
    char *answer = NULL;
    if (!title || !title[0] || !start_text || !parse_moment(start_text, &start, &all_day)) {
        cJSON_Delete(args);
        return reply("error", "Chybí název nebo čas události.");
    }
    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(args, "minutes");
    time_t end = start + (cJSON_IsNumber(minutes) && minutes->valueint > 0 ? minutes->valueint : DEFAULT_MINUTES) * 60;
    if (all_day) {
        struct tm local;
        localtime_r(&start, &local);
        local.tm_mday += 1;
        local.tm_isdst = -1;
        end = mktime(&local);
    }
    take_lock();
    if (!discover()) {
        answer = reply("error", last_error[0] ? last_error : "Kalendář se nepodařilo otevřít.");
    } else {
        char uid[48];
        snprintf(uid, sizeof(uid), "%08lx%08lx-jarvis", (unsigned long) esp_random(), (unsigned long) esp_random());
        const dav_event_t event = {
            .uid = uid,
            .title = title,
            .place = argument(args, "place"),
            .start = start,
            .end = end,
            .all_day = all_day,
            .stamp = time(NULL),
        };
        char *text = dav_build_event(&event);
        int chosen = choose_calendar(argument(args, "calendar"));
        int status = 0;
        for (int attempt = 0; text && attempt < calendar_count; attempt++) {
            int index = (chosen + attempt) % calendar_count;
            char url[DAV_HREF + 96 + 64];
            char target[DAV_HREF + 96];
            dav_join_url(origin, calendars[index].href, target, sizeof(target));
            size_t size = strlen(target);
            snprintf(url, sizeof(url), "%s%s%s.ics", target, size && target[size - 1] == '/' ? "" : "/", uid);
            char auth[256];
            authorization(auth, sizeof(auth));
            const web_header_t headers[] = {
                {"Authorization", auth}, {"Content-Type", "text/calendar; charset=utf-8"}, {"If-None-Match", "*"}};
            const web_request_t request = {
                .url = url,
                .method = WEB_PUT,
                .headers = headers,
                .header_count = 3,
                .body = text,
                .limit = 4096,
                .timeout_ms = 20000,
            };
            free(web_request(&request, &status, NULL));
            if (status == 201 || status == 204) {
                struct tm local;
                localtime_r(&start, &local);
                char when[48];
                static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
                if (all_day) {
                    snprintf(when, sizeof(when), "%s %d. %d.", DAYS[local.tm_wday % 7], local.tm_mday, (local.tm_mon + 1) % 100);
                } else {
                    snprintf(when, sizeof(when), "%s %d. %d. v %d:%02d", DAYS[local.tm_wday % 7], local.tm_mday, (local.tm_mon + 1) % 100,
                             local.tm_hour, local.tm_min);
                }
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "result", "zapsáno");
                cJSON_AddStringToObject(root, "when", when);
                cJSON_AddStringToObject(root, "calendar", calendars[index].name);
                answer = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                ESP_LOGI(TAG, "event stored in %s", calendars[index].name);
                break;
            }
            ESP_LOGW(TAG, "%s refused the event with %d", calendars[index].name, status);
            if (status != 403) {
                break;
            }
        }
        free(text);
        if (!answer) {
            answer = reply("error", status == STATUS_UNAUTHORIZED ? "Apple odmítl přihlášení." : "Událost se nepodařilo zapsat.");
        }
    }
    xSemaphoreGive(lock);
    cJSON_Delete(args);
    if (answer && strstr(answer, "\"zapsáno\"")) {
        calendar_refresh_soon();
    }
    return answer;
}

cJSON *caldav_status(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "configured", caldav_configured());
    cJSON *names = cJSON_AddArrayToObject(root, "calendars");
    if (caldav_configured()) {
        take_lock();
        for (int i = 0; discovered && i < calendar_count; i++) {
            cJSON_AddItemToArray(names, cJSON_CreateString(calendars[i].name));
        }
        cJSON_AddStringToObject(root, "error", last_error);
        xSemaphoreGive(lock);
        cJSON_AddStringToObject(root, "apple_id", settings_get()->apple_id);
        cJSON_AddStringToObject(root, "chosen", settings_get()->apple_calendar);
    }
    return root;
}
