#include "meteo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

typedef struct {
    int code;
    const char *text;
} sky_t;

static const sky_t SKIES[] = {
    {0, "jasno"},
    {1, "skoro jasno"},
    {2, "polojasno"},
    {3, "zataženo"},
    {45, "mlha"},
    {48, "mrznoucí mlha"},
    {51, "slabé mrholení"},
    {53, "mrholení"},
    {55, "husté mrholení"},
    {56, "mrznoucí mrholení"},
    {57, "husté mrznoucí mrholení"},
    {61, "slabý déšť"},
    {63, "déšť"},
    {65, "silný déšť"},
    {66, "mrznoucí déšť"},
    {67, "silný mrznoucí déšť"},
    {71, "slabé sněžení"},
    {73, "sněžení"},
    {75, "husté sněžení"},
    {77, "sněhová zrna"},
    {80, "slabé přeháňky"},
    {81, "přeháňky"},
    {82, "prudké přeháňky"},
    {85, "sněhové přeháňky"},
    {86, "husté sněhové přeháňky"},
    {95, "bouřka"},
    {96, "bouřka s kroupami"},
    {99, "silná bouřka s kroupami"},
};

static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};

const char *meteo_sky(int code)
{
    for (size_t i = 0; i < sizeof(SKIES) / sizeof(SKIES[0]); i++) {
        if (SKIES[i].code == code) {
            return SKIES[i].text;
        }
    }
    return "neznámé počasí";
}

bool meteo_parse_place(const char *json, size_t length, double *latitude, double *longitude, char *name,
                       size_t capacity)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "results"), 0);
    const cJSON *north = cJSON_GetObjectItemCaseSensitive(first, "latitude");
    const cJSON *east = cJSON_GetObjectItemCaseSensitive(first, "longitude");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(first, "name");
    bool found = cJSON_IsNumber(north) && cJSON_IsNumber(east);
    if (found) {
        *latitude = north->valuedouble;
        *longitude = east->valuedouble;
        snprintf(name, capacity, "%s", cJSON_IsString(label) ? label->valuestring : "");
    }
    cJSON_Delete(root);
    return found;
}

static bool value_at(const cJSON *object, const char *key, int index, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (index >= 0) {
        item = cJSON_GetArrayItem(item, index);
    }
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

static void add_rounded(cJSON *target, const char *name, const cJSON *source, const char *key, int index)
{
    double value;
    if (value_at(source, key, index, &value)) {
        cJSON_AddNumberToObject(target, name, round(value));
    }
}

static int weekday_of(int year, int month, int day)
{
    static const int OFFSETS[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    year -= month < 3;
    return (year + year / 4 - year / 100 + year / 400 + OFFSETS[month - 1] + day) % 7;
}

char *meteo_describe(const char *json, size_t length, const char *place)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *dates = cJSON_GetObjectItemCaseSensitive(daily, "time");
    if (!cJSON_IsObject(current) || !cJSON_IsArray(dates)) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *answer = cJSON_CreateObject();
    cJSON_AddStringToObject(answer, "place", place);
    cJSON *now = cJSON_AddObjectToObject(answer, "now");
    double code;
    if (value_at(current, "weather_code", -1, &code)) {
        cJSON_AddStringToObject(now, "sky", meteo_sky((int) code));
    }
    add_rounded(now, "celsius", current, "temperature_2m", -1);
    add_rounded(now, "feels_like", current, "apparent_temperature", -1);
    add_rounded(now, "wind_kmh", current, "wind_speed_10m", -1);
    cJSON *days = cJSON_AddArrayToObject(answer, "days");
    int count = cJSON_GetArraySize(dates);
    for (int i = 0; i < count; i++) {
        const cJSON *date = cJSON_GetArrayItem(dates, i);
        int year;
        int month;
        int day;
        if (!cJSON_IsString(date) || sscanf(date->valuestring, "%d-%d-%d", &year, &month, &day) != 3 || month < 1 ||
            month > 12) {
            continue;
        }
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "date", date->valuestring);
        cJSON_AddStringToObject(entry, "weekday", i == 0 ? "dnes" : i == 1 ? "zítra" : DAYS[weekday_of(year, month, day)]);
        if (value_at(daily, "weather_code", i, &code)) {
            cJSON_AddStringToObject(entry, "sky", meteo_sky((int) code));
        }
        add_rounded(entry, "min_celsius", daily, "temperature_2m_min", i);
        add_rounded(entry, "max_celsius", daily, "temperature_2m_max", i);
        add_rounded(entry, "rain_chance_percent", daily, "precipitation_probability_max", i);
        add_rounded(entry, "wind_kmh", daily, "wind_speed_10m_max", i);
        cJSON_AddItemToArray(days, entry);
    }
    char *text = cJSON_PrintUnformatted(answer);
    cJSON_Delete(answer);
    cJSON_Delete(root);
    return text;
}
