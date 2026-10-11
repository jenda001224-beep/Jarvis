#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "geo.h"
#include "meteo.h"
#include "settings.h"
#include "web.h"

#define RESPONSE_LIMIT 32768
#define MAX_DAYS 7

static char known_place[48];
static char known_name[48];
static double known_latitude;
static double known_longitude;

static char *failure(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static bool search(const char *name)
{
    char encoded[160];
    char url[288];
    geo_encode(name, encoded, sizeof(encoded));
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=cs&format=json",
             encoded);
    size_t length = 0;
    char *body = web_get(url, RESPONSE_LIMIT, &length);
    bool found = body && meteo_parse_place(body, length, &known_latitude, &known_longitude, known_name,
                                           sizeof(known_name));
    free(body);
    return found;
}

static bool locate(const char *place)
{
    if (known_place[0] && strcmp(known_place, place) == 0) {
        return true;
    }
    char shorter[48];
    strlcpy(shorter, place, sizeof(shorter));
    size_t length = strcspn(shorter, ",-(/");
    while (length > 0 && (shorter[length - 1] == ' ' || (shorter[length - 1] >= '0' && shorter[length - 1] <= '9'))) {
        length--;
    }
    shorter[length] = 0;
    bool found = search(place) || (length > 0 && strcmp(shorter, place) != 0 && search(shorter));
    if (found) {
        strlcpy(known_place, place, sizeof(known_place));
    }
    return found;
}

char *weather_tool(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *wanted = cJSON_GetObjectItemCaseSensitive(args, "place");
    const cJSON *span = cJSON_GetObjectItemCaseSensitive(args, "days");
    char place[48];
    strlcpy(place, cJSON_IsString(wanted) && wanted->valuestring[0] ? wanted->valuestring : settings_get()->place,
            sizeof(place));
    int days = cJSON_IsNumber(span) ? span->valueint : 2;
    days = days < 1 ? 1 : days > MAX_DAYS ? MAX_DAYS : days;
    cJSON_Delete(args);
    if (!locate(place)) {
        return failure("Tohle místo jsem nenašel.");
    }
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
             "wind_speed_10m_max&timezone=auto&forecast_days=%d",
             known_latitude, known_longitude, days);
    size_t length = 0;
    char *body = web_get(url, RESPONSE_LIMIT, &length);
    char *result = body ? meteo_describe(body, length, known_name[0] ? known_name : place) : NULL;
    free(body);
    return result ? result : failure("Předpověď se nepodařilo stáhnout.");
}
