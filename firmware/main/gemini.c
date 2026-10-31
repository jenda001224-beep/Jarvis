#include "gemini.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static cJSON *text_parts(const char *text)
{
    cJSON *parts = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", text);
    cJSON_AddItemToArray(parts, part);
    return parts;
}

char *gemini_text_request(const char *system, const char *user, int max_tokens)
{
    cJSON *root = cJSON_CreateObject();
    if (system) {
        cJSON *instruction = cJSON_AddObjectToObject(root, "systemInstruction");
        cJSON_AddItemToObject(instruction, "parts", text_parts(system));
    }
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "role", "user");
    cJSON_AddItemToObject(content, "parts", text_parts(user));
    cJSON_AddItemToArray(contents, content);
    cJSON *config = cJSON_AddObjectToObject(root, "generationConfig");
    cJSON_AddNumberToObject(config, "temperature", 0.2);
    cJSON_AddNumberToObject(config, "maxOutputTokens", max_tokens);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *gemini_text_answer(const char *response, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(response, length);
    if (!root) {
        return NULL;
    }
    char *answer = NULL;
    size_t used = 0;
    const cJSON *candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
    const cJSON *first = cJSON_IsArray(candidates) ? cJSON_GetArrayItem(candidates, 0) : NULL;
    const cJSON *reason = first ? cJSON_GetObjectItemCaseSensitive(first, "finishReason") : NULL;
    if (cJSON_IsString(reason) && strcmp(reason->valuestring, "STOP") != 0) {
        cJSON_Delete(root);
        return NULL;
    }
    const cJSON *content = first ? cJSON_GetObjectItemCaseSensitive(first, "content") : NULL;
    const cJSON *parts = content ? cJSON_GetObjectItemCaseSensitive(content, "parts") : NULL;
    const cJSON *part;
    cJSON_ArrayForEach(part, parts)
    {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(part, "text");
        const cJSON *thought = cJSON_GetObjectItemCaseSensitive(part, "thought");
        if (!cJSON_IsString(text) || cJSON_IsTrue(thought)) {
            continue;
        }
        size_t size = strlen(text->valuestring);
        char *grown = realloc(answer, used + size + 1);
        if (!grown) {
            free(answer);
            answer = NULL;
            break;
        }
        answer = grown;
        memcpy(answer + used, text->valuestring, size + 1);
        used += size;
    }
    cJSON_Delete(root);
    return answer;
}
