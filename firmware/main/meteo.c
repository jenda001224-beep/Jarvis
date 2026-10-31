#include "meteo.h"

#define RAIN_THRESHOLD_MM 0.1

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

bool meteo_current(const char *json, size_t length, int *celsius, int *code)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    double temperature;
    double sky;
    bool found = value_at(current, "temperature_2m", -1, &temperature) && value_at(current, "weather_code", -1, &sky);
    if (found) {
        *celsius = (int) round(temperature);
        *code = (int) sky;
    }
    cJSON_Delete(root);
    return found;
}

int meteo_rain_in(const char *json, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *quarter = cJSON_GetObjectItemCaseSensitive(root, "minutely_15");
    const cJSON *rain = quarter ? cJSON_GetObjectItemCaseSensitive(quarter, "precipitation") : NULL;
    int minutes = -1;
    int index = 0;
    const cJSON *slot;
    cJSON_ArrayForEach(slot, rain)
    {
        if (cJSON_IsNumber(slot) && slot->valuedouble >= RAIN_THRESHOLD_MM) {
            minutes = index * 15;
            break;
        }
        index++;
    }
    cJSON_Delete(root);
    return minutes;
}

static bool clock_minutes(const cJSON *moments, int *minutes)
{
    const cJSON *first = cJSON_IsArray(moments) ? cJSON_GetArrayItem(moments, 0) : NULL;
    const char *split = cJSON_IsString(first) ? strchr(first->valuestring, 'T') : NULL;
    int hour;
    int minute;
    if (!split || sscanf(split + 1, "%d:%d", &hour, &minute) != 2) {
        return false;
    }
    *minutes = hour * 60 + minute;
    return true;
}

bool meteo_sun(const char *json, size_t length, int *sunrise_minutes, int *sunset_minutes)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    bool found = daily && clock_minutes(cJSON_GetObjectItemCaseSensitive(daily, "sunrise"), sunrise_minutes) &&
                 clock_minutes(cJSON_GetObjectItemCaseSensitive(daily, "sunset"), sunset_minutes);
    cJSON_Delete(root);
    return found;
}

int meteo_days(const char *json, size_t length, meteo_day_t *out, int capacity)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *dates = daily ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
    int count = 0;
    for (int i = 0; cJSON_IsArray(dates) && i < cJSON_GetArraySize(dates) && count < capacity; i++) {
        const cJSON *date = cJSON_GetArrayItem(dates, i);
        meteo_day_t day = {0};
        double code;
        double high;
        double low;
        double rain = 0;
        if (!cJSON_IsString(date) || sscanf(date->valuestring, "%d-%d-%d", &day.year, &day.month, &day.day) != 3 ||
            !value_at(daily, "weather_code", i, &code) || !value_at(daily, "temperature_2m_max", i, &high) ||
            !value_at(daily, "temperature_2m_min", i, &low)) {
            continue;
        }
        value_at(daily, "precipitation_probability_max", i, &rain);
        day.code = (int) code;
        day.high = (int) round(high);
        day.low = (int) round(low);
        day.rain = (int) round(rain);
        out[count++] = day;
    }
    cJSON_Delete(root);
    return count;
}
