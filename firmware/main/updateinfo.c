#include "updateinfo.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static void read_version(const char *text, int parts[3])
{
    parts[0] = parts[1] = parts[2] = 0;
    sscanf(text, "%d.%d.%d", &parts[0], &parts[1], &parts[2]);
}

int release_compare(const char *first, const char *second)
{
    int a[3];
    int b[3];
    read_version(first, a);
    read_version(second, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

bool release_parse(const char *json, size_t length, unsigned long app_offset, release_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *version = root ? cJSON_GetObjectItemCaseSensitive(root, "version") : NULL;
    const cJSON *builds = root ? cJSON_GetObjectItemCaseSensitive(root, "builds") : NULL;
    const cJSON *build = cJSON_IsArray(builds) ? cJSON_GetArrayItem(builds, 0) : NULL;
    const cJSON *parts = build ? cJSON_GetObjectItemCaseSensitive(build, "parts") : NULL;
    bool found = false;
    const cJSON *part;
    cJSON_ArrayForEach(part, parts)
    {
        const cJSON *offset = cJSON_GetObjectItemCaseSensitive(part, "offset");
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(part, "path");
        const cJSON *md5 = cJSON_GetObjectItemCaseSensitive(part, "md5");
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(part, "size");
        if (!cJSON_IsNumber(offset) || (unsigned long) offset->valuedouble != app_offset || !cJSON_IsString(path) ||
            strlen(path->valuestring) >= sizeof(out->path)) {
            continue;
        }
        snprintf(out->path, sizeof(out->path), "%s", path->valuestring);
        if (cJSON_IsString(md5) && strlen(md5->valuestring) == sizeof(out->md5) - 1) {
            memcpy(out->md5, md5->valuestring, sizeof(out->md5));
        }
        out->size = cJSON_IsNumber(size) ? (size_t) size->valuedouble : 0;
        found = true;
        break;
    }
    if (found && cJSON_IsString(version) && strlen(version->valuestring) < sizeof(out->version)) {
        memcpy(out->version, version->valuestring, strlen(version->valuestring) + 1);
    } else {
        found = false;
    }
    cJSON_Delete(root);
    return found;
}
