#include "chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gemini.h"
#include "memory.h"
#include "recall.h"
#include "settings.h"
#include "web.h"

#define MESSAGES 24
#define MESSAGE_TEXT 1200
#define PENDING 4
#define ROUNDS 6
#define ANSWER_TOKENS 2048
#define RESPONSE_LIMIT (96 * 1024)
#define HISTORY_TURNS 12
#define MODEL_URL "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent"

static const char *const MODELS[] = {"gemini-flash-lite-latest", "gemini-3.8-flash"};

static const char WRITTEN[] =
    "\n\nPán ti teď píše textem z telefonu, nemluví s tebou nahlas. Odpovídej stručně a jako text. Může být mimo domov.";

typedef struct {
    uint32_t seq;
    int64_t when;
    char role;
    char text[MESSAGE_TEXT];
} message_t;

typedef struct {
    char *text;
    char *system;
    char *functions;
} request_t;

static const char *TAG = "chat";
static chat_tool_t run_tool;
static QueueHandle_t pending;
static SemaphoreHandle_t lock;
static EXTERNAL_RAM message_t messages[MESSAGES];
static int message_count;
static uint32_t next_seq = 1;
static volatile bool busy;
static char *history;
static int history_turns;

static void add_message(char role, const char *text)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (message_count == MESSAGES) {
        memmove(&messages[0], &messages[1], sizeof(messages[0]) * (MESSAGES - 1));
        message_count--;
    }
    message_t *message = &messages[message_count++];
    message->seq = next_seq++;
    message->when = (int64_t) time(NULL);
    message->role = role;
    strlcpy(message->text, text, sizeof(message->text));
    xSemaphoreGive(lock);
}

static char *ask_model(const char *body, int *status, size_t *length)
{
    char url[160];
    char key[sizeof(settings_get()->api_key)];
    strlcpy(key, settings_get()->api_key, sizeof(key));
    const web_header_t headers[] = {{"Content-Type", "application/json"}, {"x-goog-api-key", key}};
    for (size_t i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        snprintf(url, sizeof(url), MODEL_URL, MODELS[i]);
        const web_request_t call = {
            .url = url,
            .method = WEB_POST,
            .headers = headers,
            .header_count = 2,
            .body = body,
            .limit = RESPONSE_LIMIT,
            .timeout_ms = 25000,
        };
        int64_t t0 = esp_log_timestamp();
        char *response = web_request(&call, status, length);
        ESP_LOGI(TAG, "%s: body %u, status %d after %d ms", MODELS[i], (unsigned) strlen(body), *status, (int) (esp_log_timestamp() - t0));
        if (response && *status == 200) {
            return response;
        }
        ESP_LOGW(TAG, "%s answered %d: %.200s", MODELS[i], *status, response ? response : "");
        free(response);
    }
    return NULL;
}

static char *converse(const request_t *request, char *contents)
{
    char *tools = gemini_tools(request->functions);
    char *answer = NULL;
    for (int round = 0; round < ROUNDS && contents; round++) {
        char *body = gemini_chat_request(request->system, contents, tools, ANSWER_TOKENS);
        int status = 0;
        size_t length = 0;
        char *response = body ? ask_model(body, &status, &length) : NULL;
        free(body);
        gemini_turn_t turn;
        bool parsed = response && gemini_parse_turn(response, length, &turn);
        free(response);
        if (!parsed) {
            break;
        }
        if (turn.call_count == 0) {
            answer = turn.text;
            turn.text = NULL;
            gemini_turn_free(&turn);
            break;
        }
        char *results[GEMINI_CALLS] = {0};
        for (int i = 0; i < turn.call_count; i++) {
            ESP_LOGI(TAG, "tool %s %s", turn.calls[i].name, turn.calls[i].args);
            results[i] = run_tool(turn.calls[i].name, turn.calls[i].args);
        }
        char *with_call = gemini_append_content(contents, turn.content);
        char *with_results = with_call ? gemini_append_responses(with_call, turn.calls, results, turn.call_count) : NULL;
        free(with_call);
        free(contents);
        contents = with_results;
        for (int i = 0; i < turn.call_count; i++) {
            free(results[i]);
        }
        gemini_turn_free(&turn);
    }
    free(contents);
    free(tools);
    return answer;
}

static void remember_turn(const char *text, const char *answer)
{
    if (history_turns >= HISTORY_TURNS) {
        free(history);
        history = NULL;
        history_turns = 0;
    }
    char *with_user = gemini_append_turn(history, "user", text);
    free(history);
    history = with_user ? gemini_append_turn(with_user, "model", answer) : NULL;
    free(with_user);
    history_turns = history ? history_turns + 1 : 0;
}

static void chat_task(void *arg)
{
    (void) arg;
    request_t request;
    for (;;) {
        xQueueReceive(pending, &request, portMAX_DELAY);
        busy = true;
        char *contents = gemini_append_turn(history, "user", request.text);
        char *answer = converse(&request, contents);
        add_message('J', answer ? answer : "Teď se mi nepodařilo spojit s modelem, zkuste to prosím za chvíli.");
        if (answer) {
            remember_turn(request.text, answer);
            recall_chat_line('P', request.text);
            recall_chat_line('J', answer);
        }
        free(answer);
        free(request.text);
        free(request.system);
        free(request.functions);
        busy = uxQueueMessagesWaiting(pending) > 0;
    }
}

void chat_start(chat_tool_t tool)
{
    run_tool = tool;
    pending = xQueueCreate(PENDING, sizeof(request_t));
    lock = xSemaphoreCreateMutex();
    if (pending && lock) {
        xTaskCreatePinnedToCoreWithCaps(chat_task, "chat", 20480, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
    }
}

bool chat_send(const char *text, char *system, char *functions)
{
    size_t size = strlen(system);
    char *full = heap_caps_malloc(size + sizeof(WRITTEN), MALLOC_CAP_SPIRAM);
    request_t request = {.text = strdup(text), .system = full, .functions = functions};
    if (!pending || !lock || !request.text || !full) {
        free(request.text);
        free(full);
        free(system);
        free(functions);
        return false;
    }
    memcpy(full, system, size);
    memcpy(full + size, WRITTEN, sizeof(WRITTEN));
    free(system);
    add_message('P', text);
    busy = true;
    if (xQueueSend(pending, &request, 0) != pdTRUE) {
        free(request.text);
        free(full);
        free(functions);
        return false;
    }
    return true;
}

cJSON *chat_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "busy", busy);
    cJSON *array = cJSON_AddArrayToObject(root, "messages");
    if (!lock) {
        return root;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < message_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "seq", messages[i].seq);
        cJSON_AddNumberToObject(item, "when", (double) messages[i].when);
        cJSON_AddBoolToObject(item, "mine", messages[i].role == 'P');
        cJSON_AddStringToObject(item, "text", messages[i].text);
        cJSON_AddItemToArray(array, item);
    }
    xSemaphoreGive(lock);
    return root;
}

void chat_clear(void)
{
    if (!lock) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    message_count = 0;
    xSemaphoreGive(lock);
}

uint32_t chat_revision(void)
{
    return next_seq * 2 + (busy ? 1 : 0);
}
