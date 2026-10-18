#include "lists.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings.h"
#include "todos.h"

#define NVS_TODOS "todos"
#define NVS_ALARMS "alarms"

static SemaphoreHandle_t lock;
static volatile uint32_t revision = 1;

static const char *const DAY_NAMES[] = {"pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota", "neděle"};

static char *reply(const char *key, const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key, text);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *print(cJSON *root)
{
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static void changed(void)
{
    revision++;
    app_post(APP_EVENT_LISTS_CHANGED);
}

void lists_init(void)
{
    lock = xSemaphoreCreateMutex();
    size_t size = todos_blob_size() > alarms_blob_size() ? todos_blob_size() : alarms_blob_size();
    void *blob = malloc(size);
    if (!blob) {
        return;
    }
    todos_load(blob, settings_read_blob(NVS_TODOS, blob, todos_blob_size()));
    alarms_load(blob, settings_read_blob(NVS_ALARMS, blob, alarms_blob_size()));
    free(blob);
}

void lists_save(void)
{
    size_t size = todos_blob_size() > alarms_blob_size() ? todos_blob_size() : alarms_blob_size();
    void *blob = malloc(size);
    if (!blob) {
        return;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = todos_save(blob, size);
    xSemaphoreGive(lock);
    settings_write_blob(NVS_TODOS, blob, length);
    xSemaphoreTake(lock, portMAX_DELAY);
    length = alarms_save(blob, size);
    xSemaphoreGive(lock);
    settings_write_blob(NVS_ALARMS, blob, length);
    free(blob);
}

uint32_t lists_revision(void)
{
    return revision;
}

int lists_open_todos(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    int count = todos_open_count();
    xSemaphoreGive(lock);
    return count;
}

bool lists_due_alarm(time_t now, alarm_t *out)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool due = alarms_due(now, out);
    xSemaphoreGive(lock);
    if (due) {
        changed();
    }
    return due;
}

bool lists_next_alarm(time_t now, alarm_t *out, time_t *when)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    bool found = alarms_next(now, out, when);
    xSemaphoreGive(lock);
    return found;
}

static cJSON *todo_json(const todo_t *todo)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", todo->id);
    cJSON_AddStringToObject(item, "text", todo->text);
    cJSON_AddBoolToObject(item, "done", todo->done);
    cJSON_AddNumberToObject(item, "created", (double) todo->created);
    return item;
}

static cJSON *todos_json(void)
{
    static todo_t list[TODOS_MAX];
    xSemaphoreTake(lock, portMAX_DELAY);
    int count = todos_list(list, TODOS_MAX);
    xSemaphoreGive(lock);
    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(array, todo_json(&list[i]));
    }
    return array;
}

static void describe_days(uint8_t days, char *out, size_t capacity)
{
    if (days == 0) {
        snprintf(out, capacity, "jednou");
    } else if (days == ALARM_EVERY_DAY) {
        snprintf(out, capacity, "každý den");
    } else if (days == 0x1F) {
        snprintf(out, capacity, "všední dny");
    } else if (days == 0x60) {
        snprintf(out, capacity, "víkend");
    } else {
        out[0] = 0;
        for (int day = 0; day < 7; day++) {
            if (days & (1u << day)) {
                size_t used = strlen(out);
                snprintf(out + used, capacity - used, "%s%s", used ? ", " : "", DAY_NAMES[day]);
            }
        }
    }
}

static cJSON *alarm_json(const alarm_t *alarm, bool spoken)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", alarm->id);
    cJSON_AddNumberToObject(item, "hour", alarm->hour);
    cJSON_AddNumberToObject(item, "minute", alarm->minute);
    cJSON_AddStringToObject(item, "label", alarm->label);
    cJSON_AddBoolToObject(item, "enabled", alarm->enabled);
    if (spoken) {
        char days[96];
        describe_days(alarm->days, days, sizeof(days));
        char time_text[8];
        snprintf(time_text, sizeof(time_text), "%d:%02d", alarm->hour, alarm->minute);
        cJSON_AddStringToObject(item, "time", time_text);
        cJSON_AddStringToObject(item, "repeat", days);
    } else {
        cJSON_AddNumberToObject(item, "days", alarm->days);
    }
    return item;
}

static cJSON *alarms_json(bool spoken)
{
    static alarm_t list[ALARMS_MAX];
    xSemaphoreTake(lock, portMAX_DELAY);
    int count = alarms_list(list, ALARMS_MAX);
    xSemaphoreGive(lock);
    cJSON *array = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(array, alarm_json(&list[i], spoken));
    }
    return array;
}

static uint32_t number_of(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) && item->valuedouble > 0 ? (uint32_t) item->valuedouble : 0;
}

static const char *text_of(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

char *lists_tool_add_todo(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const char *text = text_of(args, "text");
    uint32_t id = 0;
    if (text && text[0]) {
        xSemaphoreTake(lock, portMAX_DELAY);
        id = todos_find(text);
        if (!id) {
            id = todos_add(text, (int64_t) time(NULL));
        }
        xSemaphoreGive(lock);
    }
    cJSON_Delete(args);
    if (!id) {
        return reply("error", text && text[0] ? "Seznam je plný, nejdřív něco smaž." : "Chybí, co mám zapsat.");
    }
    changed();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", "zapsáno");
    cJSON_AddNumberToObject(root, "id", id);
    return print(root);
}

char *lists_tool_list_todos(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "todos", todos_json());
    return print(root);
}

static uint32_t todo_target(const cJSON *args)
{
    uint32_t id = number_of(args, "id");
    const char *text = text_of(args, "text");
    if (!id && text) {
        xSemaphoreTake(lock, portMAX_DELAY);
        id = todos_find(text);
        xSemaphoreGive(lock);
    }
    return id;
}

char *lists_tool_complete_todo(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    uint32_t id = todo_target(args);
    cJSON_Delete(args);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool done = id && todos_set_done(id, true);
    xSemaphoreGive(lock);
    if (!done) {
        return reply("error", "Takový úkol na seznamu nemám.");
    }
    changed();
    return reply("result", "odškrtnuto");
}

char *lists_tool_remove_todo(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    uint32_t id = todo_target(args);
    cJSON_Delete(args);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool removed = id && todos_remove(id);
    xSemaphoreGive(lock);
    if (!removed) {
        return reply("error", "Takový úkol na seznamu nemám.");
    }
    changed();
    return reply("result", "smazáno");
}

static uint8_t days_from(const cJSON *array)
{
    uint8_t days = 0;
    const cJSON *day;
    cJSON_ArrayForEach(day, array)
    {
        if (cJSON_IsNumber(day) && day->valueint >= 1 && day->valueint <= 7) {
            days |= (uint8_t) (1u << (day->valueint - 1));
        }
    }
    return days;
}

char *lists_tool_set_alarm(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const char *clock = text_of(args, "time");
    const char *label = text_of(args, "label");
    int hour = -1;
    int minute = -1;
    if (clock) {
        sscanf(clock, "%d:%d", &hour, &minute);
    }
    uint8_t days = days_from(cJSON_GetObjectItemCaseSensitive(args, "days"));
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t id = alarms_add(hour, minute, days, label ? label : "");
    alarm_t alarm = {0};
    if (id) {
        alarms_get(id, &alarm);
    }
    xSemaphoreGive(lock);
    cJSON_Delete(args);
    if (!id) {
        return reply("error", hour < 0 ? "Chybí čas budíku ve tvaru HH:MM." : "Víc budíků si nezapamatuji.");
    }
    changed();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", "budík nastaven");
    cJSON_AddItemToObject(root, "alarm", alarm_json(&alarm, true));
    return print(root);
}

char *lists_tool_list_alarms(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "alarms", alarms_json(true));
    return print(root);
}

char *lists_tool_cancel_alarm(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    uint32_t id = number_of(args, "id");
    cJSON_Delete(args);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool removed = id && alarms_remove(id);
    xSemaphoreGive(lock);
    if (!removed) {
        return reply("error", "Takový budík nemám.");
    }
    changed();
    return reply("result", "budík zrušen");
}

static void answer(api_call_t *call, int status, cJSON *root)
{
    call->status = status;
    call->reply = print(root);
}

static void fail(api_call_t *call, int status, const char *message)
{
    call->status = status;
    call->reply = reply("error", message);
}

static bool read_alarm_fields(const cJSON *body, alarm_t *alarm)
{
    const cJSON *hour = cJSON_GetObjectItemCaseSensitive(body, "hour");
    const cJSON *minute = cJSON_GetObjectItemCaseSensitive(body, "minute");
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(body, "days");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(body, "label");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(body, "enabled");
    if (cJSON_IsNumber(hour)) {
        alarm->hour = (uint8_t) hour->valueint;
    }
    if (cJSON_IsNumber(minute)) {
        alarm->minute = (uint8_t) minute->valueint;
    }
    if (cJSON_IsNumber(days)) {
        alarm->days = (uint8_t) days->valueint;
    }
    if (cJSON_IsString(label)) {
        strlcpy(alarm->label, label->valuestring, sizeof(alarm->label));
    }
    if (cJSON_IsBool(enabled)) {
        alarm->enabled = cJSON_IsTrue(enabled);
    }
    return (!hour || cJSON_IsNumber(hour)) && (!minute || cJSON_IsNumber(minute)) && (!days || cJSON_IsNumber(days));
}

static bool todo_done(uint32_t id)
{
    static todo_t list[TODOS_MAX];
    int count = todos_list(list, TODOS_MAX);
    for (int i = 0; i < count; i++) {
        if (list[i].id == id) {
            return list[i].done;
        }
    }
    return false;
}

static void handle_todos(api_call_t *call, uint32_t id, const cJSON *body)
{
    bool done = false;
    if (call->method == API_GET && !id) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "todos", todos_json());
        answer(call, 200, root);
        return;
    }
    if (call->method == API_POST && !id && strcmp(call->path, "/api/todos/clear") != 0) {
        const char *text = text_of(body, "text");
        xSemaphoreTake(lock, portMAX_DELAY);
        uint32_t added = text ? todos_add(text, (int64_t) time(NULL)) : 0;
        xSemaphoreGive(lock);
        if (!added) {
            fail(call, 400, text ? "list is full" : "text missing");
            return;
        }
        id = added;
        done = true;
    } else if (call->method == API_POST && strcmp(call->path, "/api/todos/clear") == 0) {
        xSemaphoreTake(lock, portMAX_DELAY);
        todos_clear_done();
        xSemaphoreGive(lock);
        done = true;
    } else if (call->method == API_PATCH && id) {
        const cJSON *finished = cJSON_GetObjectItemCaseSensitive(body, "done");
        const char *text = text_of(body, "text");
        xSemaphoreTake(lock, portMAX_DELAY);
        bool known = todos_set_done(id, cJSON_IsBool(finished) ? cJSON_IsTrue(finished) : todo_done(id));
        done = known && (!text || todos_rename(id, text));
        xSemaphoreGive(lock);
        if (!done) {
            fail(call, 404, "no such to-do");
            return;
        }
    } else if (call->method == API_DELETE && id) {
        xSemaphoreTake(lock, portMAX_DELAY);
        done = todos_remove(id);
        xSemaphoreGive(lock);
        if (!done) {
            fail(call, 404, "no such to-do");
            return;
        }
    }
    if (!done) {
        fail(call, 404, "not found");
        return;
    }
    revision++;
    lists_save();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "todos", todos_json());
    answer(call, 200, root);
}

static void handle_alarms(api_call_t *call, uint32_t id, const cJSON *body)
{
    bool done = false;
    if (call->method == API_GET && !id) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "alarms", alarms_json(false));
        answer(call, 200, root);
        return;
    }
    if (call->method == API_POST && !id) {
        alarm_t alarm = {.hour = 255, .minute = 255, .enabled = true};
        if (!read_alarm_fields(body, &alarm)) {
            fail(call, 400, "bad alarm");
            return;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        uint32_t added = alarms_add(alarm.hour, alarm.minute, alarm.days, alarm.label);
        if (added && !alarm.enabled) {
            alarms_update(added, alarm.hour, alarm.minute, alarm.days, alarm.label, false);
        }
        xSemaphoreGive(lock);
        if (!added) {
            fail(call, 400, "bad alarm or list full");
            return;
        }
        done = true;
    } else if (call->method == API_PATCH && id) {
        xSemaphoreTake(lock, portMAX_DELAY);
        alarm_t alarm;
        bool known = alarms_get(id, &alarm);
        bool parsed = known && read_alarm_fields(body, &alarm);
        done = parsed && alarms_update(id, alarm.hour, alarm.minute, alarm.days, alarm.label, alarm.enabled);
        xSemaphoreGive(lock);
        if (!done) {
            fail(call, known ? 400 : 404, known ? "bad alarm" : "no such alarm");
            return;
        }
    } else if (call->method == API_DELETE && id) {
        xSemaphoreTake(lock, portMAX_DELAY);
        done = alarms_remove(id);
        xSemaphoreGive(lock);
        if (!done) {
            fail(call, 404, "no such alarm");
            return;
        }
    }
    if (!done) {
        fail(call, 404, "not found");
        return;
    }
    revision++;
    lists_save();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "alarms", alarms_json(false));
    answer(call, 200, root);
}

bool lists_handle(api_call_t *call)
{
    const char *path = call->path;
    bool todos = strncmp(path, "/api/todos", 10) == 0 && (path[10] == 0 || path[10] == '/');
    bool alarms = strncmp(path, "/api/alarms", 11) == 0 && (path[11] == 0 || path[11] == '/');
    if (!todos && !alarms) {
        return false;
    }
    const char *tail = path + (todos ? 10 : 11);
    uint32_t id = tail[0] == '/' ? (uint32_t) strtoul(tail + 1, NULL, 10) : 0;
    cJSON *body = call->body ? cJSON_Parse(call->body) : NULL;
    if (call->body && !body) {
        fail(call, 400, "invalid JSON");
        return true;
    }
    if (todos) {
        handle_todos(call, id, body);
    } else {
        handle_alarms(call, id, body);
    }
    cJSON_Delete(body);
    return true;
}
