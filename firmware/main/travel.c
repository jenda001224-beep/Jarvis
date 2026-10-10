#include "travel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "geo.h"
#include "settings.h"
#include "web.h"

#define RESPONSE_LIMIT 32768
#define GEOCODER_GAP_MS 1100
#define SCOOTER_METERS_PER_SECOND 4.7

typedef struct {
    const char *key;
    const char *profile;
    const char *spoken;
} transport_t;

static const transport_t MODES[] = {
    {"scooter", "bike", "na elektrokoloběžce"},
    {"bike", "bike", "na kole"},
    {"walk", "foot", "pěšky"},
    {"car", "car", "autem bez zácpy"},
};

static bool home_known;
static double home_latitude;
static double home_longitude;
static int64_t last_geocoding;

static char *reply(const char *key, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key, message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

bool travel_configured(void)
{
    return settings_get()->home[0] != 0;
}

static bool locate(const char *query, bool near_home, double *latitude, double *longitude)
{
    char encoded[480];
    char url[720];
    geo_encode(query, encoded, sizeof(encoded));
    int written = snprintf(url, sizeof(url),
                           "https://nominatim.openstreetmap.org/search?q=%s&format=jsonv2&limit=1&accept-language=cs",
                           encoded);
    if (near_home) {
        snprintf(url + written, sizeof(url) - (size_t) written, "&viewbox=%.3f,%.3f,%.3f,%.3f", home_longitude - 0.6,
                 home_latitude + 0.4, home_longitude + 0.6, home_latitude - 0.4);
    }
    int64_t wait = GEOCODER_GAP_MS - (esp_timer_get_time() / 1000 - last_geocoding);
    if (last_geocoding && wait > 0) {
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
    size_t length = 0;
    char *body = web_get(url, RESPONSE_LIMIT, &length);
    last_geocoding = esp_timer_get_time() / 1000;
    bool found = body && geo_parse_place(body, length, latitude, longitude);
    free(body);
    return found;
}

static bool locate_destination(const char *destination, double *latitude, double *longitude)
{
    if (locate(destination, true, latitude, longitude)) {
        return true;
    }
    const char *comma = strchr(destination, ',');
    while (comma && (*comma == ',' || *comma == ' ')) {
        comma++;
    }
    return comma && *comma && locate(comma, true, latitude, longitude);
}

char *travel_tool(const char *args_json)
{
    const settings_t *settings = settings_get();
    if (!settings->home[0]) {
        return reply("error", "Chybí adresa domova v nastavení.");
    }
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *destination = cJSON_GetObjectItemCaseSensitive(args, "destination");
    const cJSON *wanted = cJSON_GetObjectItemCaseSensitive(args, "mode");
    const transport_t *mode = &MODES[0];
    for (size_t i = 0; i < sizeof(MODES) / sizeof(MODES[0]); i++) {
        if (strcmp(settings->transport, MODES[i].key) == 0) {
            mode = &MODES[i];
        }
    }
    for (size_t i = 0; i < sizeof(MODES) / sizeof(MODES[0]); i++) {
        if (cJSON_IsString(wanted) && strcmp(wanted->valuestring, MODES[i].key) == 0) {
            mode = &MODES[i];
        }
    }
    char *result = NULL;
    double latitude = 0;
    double longitude = 0;
    if (!cJSON_IsString(destination) || !destination->valuestring[0]) {
        result = reply("error", "Chybí cíl cesty.");
    } else if (!home_known && !(home_known = locate(settings->home, false, &home_latitude, &home_longitude))) {
        result = reply("error", "Adresu domova se nepodařilo najít na mapě.");
    } else if (!locate_destination(destination->valuestring, &latitude, &longitude)) {
        result = reply("error", "Cíl se nepodařilo najít na mapě. Zeptej se pána na adresu.");
    } else {
        char url[224];
        snprintf(url, sizeof(url),
                 "https://routing.openstreetmap.de/routed-%s/route/v1/driving/%.5f,%.5f;%.5f,%.5f?overview=false",
                 mode->profile, home_longitude, home_latitude, longitude, latitude);
        size_t length = 0;
        double seconds = 0;
        double meters = 0;
        char *body = web_get(url, RESPONSE_LIMIT, &length);
        if (body && geo_parse_route(body, length, &seconds, &meters)) {
            if (mode == &MODES[0]) {
                seconds = meters / SCOOTER_METERS_PER_SECOND;
            }
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "minutes", ceil(seconds / 60.0));
            cJSON_AddNumberToObject(root, "kilometers", round(meters / 100.0) / 10.0);
            cJSON_AddStringToObject(root, "how", mode->spoken);
            cJSON_AddStringToObject(root, "note", "odhad z mapy, bez čekání na semaforech");
            result = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
        } else {
            result = reply("error", "Trasu se nepodařilo spočítat.");
        }
        free(body);
    }
    cJSON_Delete(args);
    return result;
}
