#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "settings.h"
#include "tavily.h"
#include "web.h"

#define SEARCH_URL "https://api.tavily.com/search"
#define RESPONSE_LIMIT 65536
#define STATUS_UNAUTHORIZED 401
#define STATUS_TOO_MANY 429
#define STATUS_PLAN_LIMIT 432
#define STATUS_PAYGO_LIMIT 433

static const char *TAG = "search";

static char *failure(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

bool search_configured(void)
{
    return settings_get()->search_key[0] != 0;
}

char *search_tool(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *query = cJSON_GetObjectItemCaseSensitive(args, "query");
    if (!cJSON_IsString(query) || !query->valuestring[0]) {
        cJSON_Delete(args);
        return failure("Chybí, co mám hledat.");
    }
    ESP_LOGI(TAG, "searching: %s", query->valuestring);
    char *request = tavily_request(query->valuestring);
    cJSON_Delete(args);
    if (!request) {
        return failure("Hledání se nepovedlo.");
    }
    char authorization[112];
    snprintf(authorization, sizeof(authorization), "Bearer %s", settings_get()->search_key);
    int status = 0;
    size_t length = 0;
    char *response = web_post(SEARCH_URL, authorization, request, RESPONSE_LIMIT, &status, &length);
    free(request);
    if (!response) {
        ESP_LOGW(TAG, "search failed with status %d", status);
        if (status == STATUS_UNAUTHORIZED) {
            return failure("Klíč k vyhledávání neplatí. Pán ho má zkontrolovat v nastavení.");
        }
        if (status == STATUS_TOO_MANY || status == STATUS_PLAN_LIMIT || status == STATUS_PAYGO_LIMIT) {
            return failure("Hledání na tento měsíc je vyčerpané.");
        }
        return failure("Vyhledávač neodpovídá.");
    }
    char *summary = tavily_summarize(response, length);
    free(response);
    return summary ? summary : failure("Nic jsem nenašel.");
}
