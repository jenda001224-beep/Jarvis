#include "ikea.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mdns.h"
#include "memory.h"
#include "net.h"
#include "proto.h"

#define MAX_DEVICES 32
#define RESPONSE_LIMIT (768 * 1024)
#define VERIFIER_LENGTH 128
#define PAIRING_ATTEMPTS 30
#define PAIRING_INTERVAL_MS 2000
#define REFRESH_INTERVAL_MS (5 * 60 * 1000)
#define SEARCH_INTERVAL_MS (60 * 1000)

typedef struct {
    char id[56];
    char name[48];
    char room[32];
    bool light;
    bool on;
    bool reachable;
    int level;
} device_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} body_t;

static const char *TAG = "ikea";

static char hub_address[48];
static char token[1536];
static EXTERNAL_RAM device_t devices[MAX_DEVICES];
static EXTERNAL_RAM device_t fresh[MAX_DEVICES];
static int device_count;
static volatile ikea_state_t state = IKEA_SEARCHING;
static volatile bool pairing_requested;
static SemaphoreHandle_t lock;
static SemaphoreHandle_t table_lock;
static TaskHandle_t task;
static esp_http_client_handle_t http;
static body_t response;

static void set_state(ikea_state_t next)
{
    if (state != next) {
        state = next;
        app_post(APP_EVENT_HUB_CHANGED);
    }
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    size_t needed = response.length + (size_t) event->data_len + 1;
    if (needed > RESPONSE_LIMIT) {
        return ESP_FAIL;
    }
    if (needed > response.capacity) {
        size_t capacity = response.capacity ? response.capacity : 8192;
        while (capacity < needed) {
            capacity *= 2;
        }
        char *grown = heap_caps_realloc(response.data, capacity, MALLOC_CAP_SPIRAM);
        if (!grown) {
            return ESP_ERR_NO_MEM;
        }
        response.data = grown;
        response.capacity = capacity;
    }
    memcpy(response.data + response.length, event->data, (size_t) event->data_len);
    response.length += (size_t) event->data_len;
    response.data[response.length] = 0;
    return ESP_OK;
}

static void drop_connection(void)
{
    if (http) {
        esp_http_client_cleanup(http);
        http = NULL;
    }
}

static int perform(esp_http_client_method_t method, const char *path, const char *content_type, const char *body,
                   bool authorised)
{
    char url[320];
    snprintf(url, sizeof(url), "https://%s:8443/v1%s", hub_address, path);
    if (!http) {
        const esp_http_client_config_t config = {
            .url = url,
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .skip_cert_common_name_check = true,
            .timeout_ms = 8000,
            .buffer_size = 4096,
            .buffer_size_tx = 2560,
            .event_handler = on_http_event,
            .keep_alive_enable = true,
        };
        http = esp_http_client_init(&config);
        if (!http) {
            return -1;
        }
    }
    esp_http_client_set_url(http, url);
    esp_http_client_set_method(http, method);
    esp_http_client_delete_header(http, "Authorization");
    esp_http_client_delete_header(http, "Content-Type");
    if (authorised) {
        static EXTERNAL_RAM char bearer[sizeof(token) + 8];
        snprintf(bearer, sizeof(bearer), "Bearer %s", token);
        esp_http_client_set_header(http, "Authorization", bearer);
    }
    if (content_type) {
        esp_http_client_set_header(http, "Content-Type", content_type);
    }
    esp_http_client_set_post_field(http, body, body ? (int) strlen(body) : 0);
    response.length = 0;
    if (response.data) {
        response.data[0] = 0;
    }
    if (esp_http_client_perform(http) != ESP_OK) {
        drop_connection();
        return -1;
    }
    return esp_http_client_get_status_code(http);
}

static int request(esp_http_client_method_t method, const char *path, const char *content_type, const char *body,
                   bool authorised)
{
    int status = perform(method, path, content_type, body, authorised);
    if (status < 0) {
        status = perform(method, path, content_type, body, authorised);
    }
    return status;
}

static bool discover(void)
{
    mdns_result_t *results = NULL;
    if (mdns_query_ptr("_ihsp", "_tcp", 3000, 4, &results) != ESP_OK || !results) {
        return false;
    }
    bool found = false;
    for (mdns_result_t *result = results; result && !found; result = result->next) {
        for (mdns_ip_addr_t *address = result->addr; address; address = address->next) {
            if (address->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(hub_address, sizeof(hub_address), IPSTR, IP2STR(&address->addr.u_addr.ip4));
                found = true;
                break;
            }
        }
    }
    mdns_query_results_free(results);
    if (found) {
        ESP_LOGI(TAG, "hub at %s", hub_address);
    }
    return found;
}

static bool response_string(const char *key, char *out, size_t capacity)
{
    cJSON *root = response.data ? cJSON_ParseWithLength(response.data, response.length) : NULL;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    bool ok = cJSON_IsString(item) && strlen(item->valuestring) < capacity;
    if (ok) {
        strcpy(out, item->valuestring);
    }
    cJSON_Delete(root);
    return ok;
}

static bool pair(void)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    char verifier[VERIFIER_LENGTH + 1];
    for (int i = 0; i < VERIFIER_LENGTH; i++) {
        verifier[i] = ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
    }
    verifier[VERIFIER_LENGTH] = 0;

    uint8_t digest[32];
    mbedtls_sha256((const uint8_t *) verifier, VERIFIER_LENGTH, digest, 0);
    char challenge[48];
    size_t length = proto_base64_encode(digest, sizeof(digest), challenge);
    while (length > 0 && challenge[length - 1] == '=') {
        length--;
    }
    challenge[length] = 0;
    for (size_t i = 0; i < length; i++) {
        if (challenge[i] == '+') {
            challenge[i] = '-';
        } else if (challenge[i] == '/') {
            challenge[i] = '_';
        }
    }

    char path[256];
    snprintf(path, sizeof(path),
             "/oauth/authorize?audience=homesmart.local&response_type=code&code_challenge=%s"
             "&code_challenge_method=S256",
             challenge);
    char code[160];
    if (request(HTTP_METHOD_GET, path, NULL, NULL, false) != 200 || !response_string("code", code, sizeof(code))) {
        ESP_LOGW(TAG, "hub refused the pairing request");
        return false;
    }

    char form[sizeof(code) + VERIFIER_LENGTH + 96];
    snprintf(form, sizeof(form), "code=%s&name=Jarvis&grant_type=authorization_code&code_verifier=%s", code,
             verifier);
    for (int attempt = 0; attempt < PAIRING_ATTEMPTS; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(PAIRING_INTERVAL_MS));
        if (request(HTTP_METHOD_POST, "/oauth/token", "application/x-www-form-urlencoded", form, false) == 200 &&
            response_string("access_token", token, sizeof(token))) {
            app_post(APP_EVENT_HUB_PAIRED);
            ESP_LOGI(TAG, "paired");
            return true;
        }
    }
    token[0] = 0;
    return false;
}

static void copy_field(char *target, size_t capacity, const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    strlcpy(target, cJSON_IsString(item) ? item->valuestring : "", capacity);
}

static int refresh(void)
{
    int status = request(HTTP_METHOD_GET, "/devices", NULL, NULL, true);
    if (status != 200) {
        return status;
    }
    cJSON *root = cJSON_ParseWithLength(response.data, response.length);
    int count = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, root)
    {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(entry, "type");
        if (!cJSON_IsString(type) || count >= MAX_DEVICES) {
            continue;
        }
        bool light = strcmp(type->valuestring, "light") == 0;
        if (!light && strcmp(type->valuestring, "outlet") != 0) {
            continue;
        }
        const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(entry, "attributes");
        device_t *device = &fresh[count];
        copy_field(device->id, sizeof(device->id), entry, "id");
        copy_field(device->name, sizeof(device->name), attributes, "customName");
        copy_field(device->room, sizeof(device->room), cJSON_GetObjectItemCaseSensitive(entry, "room"), "name");
        device->light = light;
        device->on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(attributes, "isOn"));
        device->reachable = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "isReachable"));
        const cJSON *level = cJSON_GetObjectItemCaseSensitive(attributes, "lightLevel");
        device->level = cJSON_IsNumber(level) ? level->valueint : -1;
        if (device->id[0]) {
            count++;
        }
    }
    cJSON_Delete(root);
    xSemaphoreTake(table_lock, portMAX_DELAY);
    memcpy(devices, fresh, sizeof(devices));
    device_count = count;
    xSemaphoreGive(table_lock);
    ESP_LOGI(TAG, "%d lights and outlets", count);
    return status;
}

static void ikea_task(void *arg)
{
    for (;;) {
        uint32_t wait_ms = SEARCH_INTERVAL_MS;
        if (!net_connected()) {
            wait_ms = 2000;
        } else {
            xSemaphoreTake(lock, portMAX_DELAY);
            if (!hub_address[0] && !discover()) {
                set_state(IKEA_NOT_FOUND);
            } else if (pairing_requested || !token[0]) {
                if (pairing_requested) {
                    pairing_requested = false;
                    set_state(IKEA_PAIRING);
                    if (pair()) {
                        refresh();
                        set_state(IKEA_READY);
                        wait_ms = REFRESH_INTERVAL_MS;
                    } else {
                        set_state(IKEA_UNPAIRED);
                    }
                } else {
                    set_state(IKEA_UNPAIRED);
                }
            } else {
                int status = refresh();
                if (status == 200) {
                    set_state(IKEA_READY);
                    wait_ms = REFRESH_INTERVAL_MS;
                } else if (status == 401 || status == 403) {
                    token[0] = 0;
                    set_state(IKEA_UNPAIRED);
                } else {
                    hub_address[0] = 0;
                    wait_ms = 10000;
                }
            }
            drop_connection();
            xSemaphoreGive(lock);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

void ikea_start(const char *stored_token)
{
    strlcpy(token, stored_token ? stored_token : "", sizeof(token));
    lock = xSemaphoreCreateMutex();
    table_lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCoreWithCaps(ikea_task, "ikea", 8192, NULL, 3, &task, 0, MALLOC_CAP_SPIRAM);
}

const char *ikea_token(void)
{
    return token;
}

ikea_state_t ikea_state(void)
{
    return state;
}

int ikea_device_count(void)
{
    return device_count;
}

void ikea_request_pairing(void)
{
    pairing_requested = true;
    if (task) {
        xTaskNotifyGive(task);
    }
}

char *ikea_describe_devices(void)
{
    if (state != IKEA_READY || device_count == 0) {
        return NULL;
    }
    size_t capacity = 256 + (size_t) device_count * 200;
    char *text = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
    if (!text) {
        return NULL;
    }
    xSemaphoreTake(table_lock, portMAX_DELAY);
    size_t used = (size_t) snprintf(text, capacity, "Zařízení (id; název; místnost; typ):\n");
    for (int i = 0; i < device_count && used < capacity; i++) {
        const device_t *device = &devices[i];
        used += (size_t) snprintf(text + used, capacity - used, "%s; %s; %s; %s\n", device->id, device->name,
                                  device->room[0] ? device->room : "bez místnosti",
                                  device->light ? "světlo" : "zásuvka");
    }
    xSemaphoreGive(table_lock);
    return text;
}

static char *print_and_free(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static char *error_result(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    return print_and_free(root);
}

char *ikea_tool_get(void)
{
    if (state != IKEA_READY) {
        return error_result("IKEA hub není spárovaný");
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    int status = refresh();
    cJSON *root = cJSON_CreateObject();
    if (status != 200) {
        cJSON_AddStringToObject(root, "warning", "hub neodpověděl, stav může být starý");
    }
    cJSON *list = cJSON_AddArrayToObject(root, "devices");
    for (int i = 0; i < device_count; i++) {
        const device_t *device = &devices[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", device->id);
        cJSON_AddStringToObject(item, "name", device->name);
        cJSON_AddStringToObject(item, "room", device->room);
        cJSON_AddStringToObject(item, "type", device->light ? "light" : "outlet");
        cJSON_AddBoolToObject(item, "on", device->on);
        cJSON_AddBoolToObject(item, "reachable", device->reachable);
        if (device->level >= 0) {
            cJSON_AddNumberToObject(item, "brightness", device->level);
        }
        cJSON_AddItemToArray(list, item);
    }
    xSemaphoreGive(lock);
    return print_and_free(root);
}

static device_t *find_device(const char *id)
{
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].id, id) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

static bool patch(const device_t *device, const char *attribute_json)
{
    char path[96];
    char body[96];
    snprintf(path, sizeof(path), "/devices/%s", device->id);
    snprintf(body, sizeof(body), "[{\"attributes\":{%s}}]", attribute_json);
    int status = request(HTTP_METHOD_PATCH, path, "application/json", body, true);
    return status >= 200 && status < 300;
}

char *ikea_tool_set(const char *args_json)
{
    if (state != IKEA_READY) {
        return error_result("IKEA hub není spárovaný");
    }
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(args, "ids");
    const cJSON *on = cJSON_GetObjectItemCaseSensitive(args, "on");
    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(args, "brightness");
    const cJSON *hue = cJSON_GetObjectItemCaseSensitive(args, "hue");
    const cJSON *saturation = cJSON_GetObjectItemCaseSensitive(args, "saturation");
    const cJSON *kelvin = cJSON_GetObjectItemCaseSensitive(args, "kelvin");
    bool anything = cJSON_IsBool(on) || cJSON_IsNumber(brightness) || cJSON_IsNumber(hue) || cJSON_IsNumber(kelvin);
    if (!cJSON_IsArray(ids) || !anything) {
        cJSON_Delete(args);
        return error_result("chybí ids a aspoň jedno z on, brightness, hue, kelvin");
    }
    int changed = 0;
    int failed = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    const cJSON *id;
    cJSON_ArrayForEach(id, ids)
    {
        device_t *device = cJSON_IsString(id) ? find_device(id->valuestring) : NULL;
        if (!device) {
            failed++;
            continue;
        }
        bool ok = true;
        if (cJSON_IsBool(on)) {
            ok = patch(device, cJSON_IsTrue(on) ? "\"isOn\":true" : "\"isOn\":false");
        }
        if (ok && cJSON_IsNumber(brightness) && device->light) {
            int level = brightness->valueint < 1 ? 1 : brightness->valueint > 100 ? 100 : brightness->valueint;
            char attribute[32];
            snprintf(attribute, sizeof(attribute), "\"lightLevel\":%d", level);
            ok = patch(device, attribute);
        }
        if (ok && cJSON_IsNumber(hue) && device->light) {
            double degrees = hue->valuedouble < 0 ? 0 : hue->valuedouble > 360 ? 360 : hue->valuedouble;
            double amount = cJSON_IsNumber(saturation) ? saturation->valuedouble : 100.0;
            amount = amount < 0 ? 0 : amount > 100 ? 100 : amount;
            char attribute[72];
            snprintf(attribute, sizeof(attribute), "\"colorHue\":%d,\"colorSaturation\":%.2f", (int) degrees,
                     amount / 100.0);
            ok = patch(device, attribute);
        } else if (ok && cJSON_IsNumber(kelvin) && device->light) {
            int value = kelvin->valueint < 2000 ? 2000 : kelvin->valueint > 6500 ? 6500 : kelvin->valueint;
            char attribute[40];
            snprintf(attribute, sizeof(attribute), "\"colorTemperature\":%d", value);
            ok = patch(device, attribute);
        }
        if (ok) {
            changed++;
        } else {
            failed++;
        }
    }
    drop_connection();
    xSemaphoreGive(lock);
    cJSON_Delete(args);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", failed == 0 ? "ok" : changed ? "partial" : "failed");
    cJSON_AddNumberToObject(root, "changed", changed);
    cJSON_AddNumberToObject(root, "failed", failed);
    return print_and_free(root);
}
