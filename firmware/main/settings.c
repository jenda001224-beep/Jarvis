#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_PARTITION_TYPE 0x40
#define NVS_NAMESPACE "jarvis"
#define NVS_HUB_TOKEN "hub_token"
#define CALENDAR_LINK_LIMIT 256

static const char *TAG = "settings";

static settings_t settings = {
    .model = "gemini-3.8-live",
    .voice = "Charon",
    .place = "Praha",
    .transport = "scooter",
    .volume = 70,
    .sensitivity = 1,
    .search = true,
};

static void copy_string(char *target, size_t capacity, const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring[0]) {
        strlcpy(target, item->valuestring, capacity);
    }
}

typedef struct {
    const char *key;
    size_t limit;
    bool required;
} stored_text_t;

static const stored_text_t STORED_TEXTS[] = {
    {"ssid", sizeof(settings.ssid), true},
    {"password", sizeof(settings.password), false},
    {"key", sizeof(settings.api_key), true},
    {"search_key", sizeof(settings.search_key), false},
    {"voice", sizeof(settings.voice), false},
    {"place", sizeof(settings.place), false},
    {"home", sizeof(settings.home), false},
    {"transport", sizeof(settings.transport), false},
    {"printer_code", sizeof(settings.printer_code), false},
    {"printer_ip", sizeof(settings.printer_address), false},
    {"printer_serial", sizeof(settings.printer_serial), false},
};

static const esp_partition_t *config_partition(void)
{
    return esp_partition_find_first(CONFIG_PARTITION_TYPE, ESP_PARTITION_SUBTYPE_ANY, "cfg");
}

static char *read_stored(const esp_partition_t *partition, size_t *length)
{
    char *raw = malloc(partition->size + 1);
    if (!raw || esp_partition_read(partition, 0, raw, partition->size) != ESP_OK) {
        free(raw);
        return NULL;
    }
    size_t used = 0;
    while (used < partition->size && raw[used] != 0 && (unsigned char) raw[used] != 0xFF) {
        used++;
    }
    raw[used] = 0;
    *length = used;
    return raw;
}

static bool store_error(char *error, size_t capacity, const char *message)
{
    strlcpy(error, message, capacity);
    return false;
}

bool settings_store(const char *changes_json, char *error, size_t capacity)
{
    const esp_partition_t *partition = config_partition();
    cJSON *changes = cJSON_Parse(changes_json);
    if (!partition || !cJSON_IsObject(changes)) {
        cJSON_Delete(changes);
        return store_error(error, capacity, "invalid settings");
    }
    size_t length = 0;
    char *raw = read_stored(partition, &length);
    cJSON *root = raw && length ? cJSON_Parse(raw) : NULL;
    free(raw);
    if (!root) {
        root = cJSON_CreateObject();
    }
    bool valid = true;
    for (size_t i = 0; i < sizeof(STORED_TEXTS) / sizeof(STORED_TEXTS[0]) && valid; i++) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(changes, STORED_TEXTS[i].key);
        if (!item) {
            continue;
        }
        if (!cJSON_IsString(item) || strlen(item->valuestring) >= STORED_TEXTS[i].limit) {
            valid = store_error(error, capacity, STORED_TEXTS[i].key);
        } else if (!item->valuestring[0] && STORED_TEXTS[i].required) {
            valid = store_error(error, capacity, STORED_TEXTS[i].key);
        } else if (!item->valuestring[0]) {
            cJSON_DeleteItemFromObjectCaseSensitive(root, STORED_TEXTS[i].key);
        } else {
            cJSON_DeleteItemFromObjectCaseSensitive(root, STORED_TEXTS[i].key);
            cJSON_AddStringToObject(root, STORED_TEXTS[i].key, item->valuestring);
        }
    }
    const cJSON *search = cJSON_GetObjectItemCaseSensitive(changes, "search");
    if (valid && search) {
        if (!cJSON_IsBool(search)) {
            valid = store_error(error, capacity, "search");
        } else {
            cJSON_DeleteItemFromObjectCaseSensitive(root, "search");
            cJSON_AddBoolToObject(root, "search", cJSON_IsTrue(search));
        }
    }
    const cJSON *calendars = cJSON_GetObjectItemCaseSensitive(changes, "calendars");
    if (valid && calendars) {
        int count = cJSON_GetArraySize(calendars);
        const cJSON *link;
        cJSON_ArrayForEach(link, calendars)
        {
            if (!cJSON_IsString(link) || !link->valuestring[0] || strlen(link->valuestring) >= CALENDAR_LINK_LIMIT) {
                valid = false;
            }
        }
        if (!cJSON_IsArray(calendars) || count > SETTINGS_CALENDARS || !valid) {
            valid = store_error(error, capacity, "calendars");
        } else {
            cJSON_DeleteItemFromObjectCaseSensitive(root, "calendars");
            if (count) {
                cJSON_AddItemToObject(root, "calendars", cJSON_Duplicate(calendars, true));
            }
        }
    }
    cJSON_Delete(changes);
    char *text = valid ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!valid) {
        return false;
    }
    size_t text_length = text ? strlen(text) : 0;
    if (!text || text_length >= partition->size) {
        free(text);
        return store_error(error, capacity, "settings too long");
    }
    bool written = esp_partition_erase_range(partition, 0, partition->size) == ESP_OK &&
                   esp_partition_write(partition, 0, text, text_length + 1) == ESP_OK;
    free(text);
    return written || store_error(error, capacity, "write failed");
}

bool settings_load(void)
{
    if (nvs_flash_init() != ESP_OK) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    const esp_partition_t *partition = config_partition();
    if (!partition) {
        ESP_LOGE(TAG, "cfg partition missing");
        return false;
    }
    size_t length = 0;
    char *raw = read_stored(partition, &length);
    if (!raw) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(raw, length);
    free(raw);
    if (!root) {
        ESP_LOGW(TAG, "no configuration stored");
        return false;
    }
    copy_string(settings.ssid, sizeof(settings.ssid), root, "ssid");
    copy_string(settings.password, sizeof(settings.password), root, "password");
    copy_string(settings.api_key, sizeof(settings.api_key), root, "key");
    copy_string(settings.search_key, sizeof(settings.search_key), root, "search_key");
    copy_string(settings.model, sizeof(settings.model), root, "model");
    copy_string(settings.voice, sizeof(settings.voice), root, "voice");
    copy_string(settings.place, sizeof(settings.place), root, "place");
    copy_string(settings.home, sizeof(settings.home), root, "home");
    copy_string(settings.transport, sizeof(settings.transport), root, "transport");
    copy_string(settings.printer_code, sizeof(settings.printer_code), root, "printer_code");
    copy_string(settings.printer_address, sizeof(settings.printer_address), root, "printer_ip");
    copy_string(settings.printer_serial, sizeof(settings.printer_serial), root, "printer_serial");
    const cJSON *calendar;
    int calendars = 0;
    cJSON_ArrayForEach(calendar, cJSON_GetObjectItemCaseSensitive(root, "calendars"))
    {
        if (calendars < SETTINGS_CALENDARS && cJSON_IsString(calendar) && calendar->valuestring[0] &&
            strlen(calendar->valuestring) < sizeof(settings.calendars[0])) {
            strcpy(settings.calendars[calendars++], calendar->valuestring);
        }
    }
    const cJSON *volume = cJSON_GetObjectItemCaseSensitive(root, "volume");
    if (cJSON_IsNumber(volume) && volume->valueint >= 0 && volume->valueint <= 100) {
        settings.volume = volume->valueint;
    }
    const cJSON *sensitivity = cJSON_GetObjectItemCaseSensitive(root, "sensitivity");
    if (cJSON_IsNumber(sensitivity) && sensitivity->valueint >= 0 && sensitivity->valueint <= 2) {
        settings.sensitivity = sensitivity->valueint;
    }
    const cJSON *search = cJSON_GetObjectItemCaseSensitive(root, "search");
    if (cJSON_IsBool(search)) {
        settings.search = cJSON_IsTrue(search);
    }
    cJSON_Delete(root);
    return settings.ssid[0] && settings.api_key[0];
}

const settings_t *settings_get(void)
{
    return &settings;
}

bool settings_read_hub_token(char *out, size_t capacity)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = capacity;
    esp_err_t err = nvs_get_str(handle, NVS_HUB_TOKEN, out, &length);
    nvs_close(handle);
    return err == ESP_OK && out[0];
}

void settings_write_hub_token(const char *token)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, NVS_HUB_TOKEN, token);
    nvs_commit(handle);
    nvs_close(handle);
}

size_t settings_read_blob(const char *name, void *out, size_t capacity)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    size_t length = capacity;
    esp_err_t err = nvs_get_blob(handle, name, out, &length);
    nvs_close(handle);
    return err == ESP_OK ? length : 0;
}

void settings_write_blob(const char *name, const void *data, size_t length)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, name, data, length);
    nvs_commit(handle);
    nvs_close(handle);
}

uint32_t settings_read_number(const char *name)
{
    nvs_handle_t handle;
    uint32_t value = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, name, &value);
        nvs_close(handle);
    }
    return value;
}

void settings_write_number(const char *name, uint32_t value)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_u32(handle, name, value);
    nvs_commit(handle);
    nvs_close(handle);
}
