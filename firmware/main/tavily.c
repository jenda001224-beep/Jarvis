#include "tavily.h"

#include <stdbool.h>
#include <string.h>

#include "cJSON.h"

#define RESULTS 5
#define CONTENT_LIMIT 600
#define ANSWER_LIMIT 1200

static void add_trimmed(cJSON *object, const char *name, const char *text, size_t limit)
{
    size_t length = strlen(text);
    if (length <= limit) {
        cJSON_AddStringToObject(object, name, text);
        return;
    }
    while (limit > 0 && ((unsigned char) text[limit] & 0xC0) == 0x80) {
        limit--;
    }
    char *cut = cJSON_malloc(limit + 4);
    if (!cut) {
        return;
    }
    memcpy(cut, text, limit);
    memcpy(cut + limit, "…", 4);
    cJSON_AddStringToObject(object, name, cut);
    cJSON_free(cut);
}

char *tavily_request(const char *query)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "query", query);
    cJSON_AddStringToObject(root, "search_depth", "basic");
    cJSON_AddStringToObject(root, "include_answer", "basic");
    cJSON_AddNumberToObject(root, "max_results", RESULTS);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *tavily_summarize(const char *json, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!root) {
        return NULL;
    }
    const cJSON *answer = cJSON_GetObjectItemCaseSensitive(root, "answer");
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON *summary = cJSON_CreateObject();
    if (cJSON_IsString(answer) && answer->valuestring[0]) {
        add_trimmed(summary, "answer", answer->valuestring, ANSWER_LIMIT);
    }
    cJSON *found = cJSON_AddArrayToObject(summary, "results");
    const cJSON *result;
    cJSON_ArrayForEach(result, results)
    {
        if (cJSON_GetArraySize(found) >= RESULTS) {
            break;
        }
        const cJSON *title = cJSON_GetObjectItemCaseSensitive(result, "title");
        const cJSON *url = cJSON_GetObjectItemCaseSensitive(result, "url");
        const cJSON *content = cJSON_GetObjectItemCaseSensitive(result, "content");
        if (!cJSON_IsString(content) || !content->valuestring[0]) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (cJSON_IsString(title)) {
            add_trimmed(item, "title", title->valuestring, CONTENT_LIMIT);
        }
        if (cJSON_IsString(url)) {
            add_trimmed(item, "url", url->valuestring, CONTENT_LIMIT);
        }
        add_trimmed(item, "content", content->valuestring, CONTENT_LIMIT);
        cJSON_AddItemToArray(found, item);
    }
    bool useful = cJSON_GetObjectItemCaseSensitive(summary, "answer") || cJSON_GetArraySize(found) > 0;
    char *text = useful ? cJSON_PrintUnformatted(summary) : NULL;
    cJSON_Delete(summary);
    cJSON_Delete(root);
    return text;
}
