#include "geo.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

size_t geo_encode(const char *text, char *out, size_t capacity)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t used = 0;
    for (; *text; text++) {
        unsigned char c = (unsigned char) *text;
        bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                     c == '_' || c == '.' || c == '~';
        size_t needed = plain ? 1 : 3;
        if (used + needed >= capacity) {
            break;
        }
        if (plain) {
            out[used++] = (char) c;
        } else {
            out[used++] = '%';
            out[used++] = HEX[c >> 4];
            out[used++] = HEX[c & 15];
        }
    }
    if (capacity) {
        out[used] = 0;
    }
    return used;
}

static bool coordinate(const cJSON *item, double low, double high, double *out)
{
    double value;
    if (cJSON_IsNumber(item)) {
        value = item->valuedouble;
    } else if (cJSON_IsString(item) && item->valuestring[0]) {
        char *end = NULL;
        value = strtod(item->valuestring, &end);
        if (end == item->valuestring || *end) {
            return false;
        }
    } else {
        return false;
    }
    if (value < low || value > high) {
        return false;
    }
    *out = value;
    return true;
}

bool geo_parse_place(const char *json, size_t length, double *latitude, double *longitude)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *first = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
    bool found = coordinate(cJSON_GetObjectItemCaseSensitive(first, "lat"), -90, 90, latitude) &&
                 coordinate(cJSON_GetObjectItemCaseSensitive(first, "lon"), -180, 180, longitude);
    cJSON_Delete(root);
    return found;
}

bool geo_parse_route(const char *json, size_t length, double *seconds, double *meters)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *route = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "routes"), 0);
    const cJSON *duration = cJSON_GetObjectItemCaseSensitive(route, "duration");
    const cJSON *distance = cJSON_GetObjectItemCaseSensitive(route, "distance");
    bool found = cJSON_IsString(code) && strcmp(code->valuestring, "Ok") == 0 && cJSON_IsNumber(duration) &&
                 cJSON_IsNumber(distance) && duration->valuedouble >= 0 && distance->valuedouble >= 0;
    if (found) {
        *seconds = duration->valuedouble;
        *meters = distance->valuedouble;
    }
    cJSON_Delete(root);
    return found;
}
