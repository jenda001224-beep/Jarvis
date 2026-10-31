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

char *gemini_tools(const char *functions_json)
{
    cJSON *functions = cJSON_Parse(functions_json);
    if (!cJSON_IsArray(functions)) {
        cJSON_Delete(functions);
        return NULL;
    }
    cJSON *function;
    cJSON_ArrayForEach(function, functions)
    {
        const cJSON *parameters = cJSON_GetObjectItemCaseSensitive(function, "parameters");
        const cJSON *properties = parameters ? cJSON_GetObjectItemCaseSensitive(parameters, "properties") : NULL;
        if (parameters && (!properties || !properties->child)) {
            cJSON_DeleteItemFromObjectCaseSensitive(function, "parameters");
        }
    }
    cJSON *root = cJSON_CreateArray();
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddItemToObject(entry, "functionDeclarations", functions);
    cJSON_AddItemToArray(root, entry);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *gemini_chat_request(const char *system, const char *contents_json, const char *tools_json, int max_tokens)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *instruction = cJSON_AddObjectToObject(root, "systemInstruction");
    cJSON_AddItemToObject(instruction, "parts", text_parts(system));
    cJSON *contents = cJSON_Parse(contents_json);
    cJSON_AddItemToObject(root, "contents", contents ? contents : cJSON_CreateArray());
    cJSON *tools = tools_json ? cJSON_Parse(tools_json) : NULL;
    if (tools) {
        cJSON_AddItemToObject(root, "tools", tools);
    }
    cJSON *config = cJSON_AddObjectToObject(root, "generationConfig");
    cJSON_AddNumberToObject(config, "maxOutputTokens", max_tokens);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

bool gemini_parse_turn(const char *response, size_t length, gemini_turn_t *turn)
{
    memset(turn, 0, sizeof(*turn));
    cJSON *root = cJSON_ParseWithLength(response, length);
    const cJSON *candidates = root ? cJSON_GetObjectItemCaseSensitive(root, "candidates") : NULL;
    const cJSON *first = cJSON_IsArray(candidates) ? cJSON_GetArrayItem(candidates, 0) : NULL;
    const cJSON *content = first ? cJSON_GetObjectItemCaseSensitive(first, "content") : NULL;
    const cJSON *parts = content ? cJSON_GetObjectItemCaseSensitive(content, "parts") : NULL;
    if (!cJSON_IsArray(parts)) {
        cJSON_Delete(root);
        return false;
    }
    turn->content = cJSON_PrintUnformatted(content);
    size_t used = 0;
    const cJSON *part;
    cJSON_ArrayForEach(part, parts)
    {
        const cJSON *call = cJSON_GetObjectItemCaseSensitive(part, "functionCall");
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(part, "text");
        if (call && turn->call_count < GEMINI_CALLS) {
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
            const cJSON *args = cJSON_GetObjectItemCaseSensitive(call, "args");
            if (cJSON_IsString(name)) {
                gemini_call_t *slot = &turn->calls[turn->call_count++];
                slot->name = strdup(name->valuestring);
                slot->args = args ? cJSON_PrintUnformatted(args) : strdup("{}");
            }
        } else if (cJSON_IsString(text) && !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(part, "thought"))) {
            size_t size = strlen(text->valuestring);
            char *grown = realloc(turn->text, used + size + 1);
            if (grown) {
                turn->text = grown;
                memcpy(turn->text + used, text->valuestring, size + 1);
                used += size;
            }
        }
    }
    cJSON_Delete(root);
    return true;
}

void gemini_turn_free(gemini_turn_t *turn)
{
    free(turn->text);
    free(turn->content);
    for (int i = 0; i < turn->call_count; i++) {
        free(turn->calls[i].name);
        free(turn->calls[i].args);
    }
    memset(turn, 0, sizeof(*turn));
}

static char *append_item(const char *contents_json, cJSON *item)
{
    cJSON *contents = contents_json ? cJSON_Parse(contents_json) : NULL;
    if (!cJSON_IsArray(contents)) {
        cJSON_Delete(contents);
        contents = cJSON_CreateArray();
    }
    cJSON_AddItemToArray(contents, item);
    char *text = cJSON_PrintUnformatted(contents);
    cJSON_Delete(contents);
    return text;
}

char *gemini_append_turn(const char *contents_json, const char *role, const char *text)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "role", role);
    cJSON_AddItemToObject(item, "parts", text_parts(text));
    return append_item(contents_json, item);
}

char *gemini_append_content(const char *contents_json, const char *content_json)
{
    cJSON *item = cJSON_Parse(content_json);
    return item ? append_item(contents_json, item) : NULL;
}

char *gemini_append_responses(const char *contents_json, const gemini_call_t *calls, char *const *results, int count)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "role", "user");
    cJSON *parts = cJSON_AddArrayToObject(item, "parts");
    for (int i = 0; i < count; i++) {
        cJSON *part = cJSON_CreateObject();
        cJSON *response = cJSON_AddObjectToObject(part, "functionResponse");
        cJSON_AddStringToObject(response, "name", calls[i].name);
        cJSON *value = results[i] ? cJSON_Parse(results[i]) : NULL;
        if (!cJSON_IsObject(value)) {
            cJSON_Delete(value);
            value = cJSON_CreateObject();
            cJSON_AddStringToObject(value, "result", results[i] ? results[i] : "");
        }
        cJSON_AddItemToObject(response, "response", value);
        cJSON_AddItemToArray(parts, part);
    }
    return append_item(contents_json, item);
}
