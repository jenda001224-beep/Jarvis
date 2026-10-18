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
#define NVS_REMINDERS "reminders"

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

bool settings_load(void)
{
    if (nvs_flash_init() != ESP_OK) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    const esp_partition_t *partition =
        esp_partition_find_first(CONFIG_PARTITION_TYPE, ESP_PARTITION_SUBTYPE_ANY, "cfg");
    if (!partition) {
        ESP_LOGE(TAG, "cfg partition missing");
        return false;
    }
    char *raw = malloc(partition->size + 1);
    if (!raw || esp_partition_read(partition, 0, raw, partition->size) != ESP_OK) {
        free(raw);
        return false;
    }
    size_t length = 0;
    while (length < partition->size && raw[length] != 0 && (unsigned char) raw[length] != 0xFF) {
        length++;
    }
    raw[length] = 0;
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

size_t settings_read_reminders(void *out, size_t capacity)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    size_t length = capacity;
    esp_err_t err = nvs_get_blob(handle, NVS_REMINDERS, out, &length);
    nvs_close(handle);
    return err == ESP_OK ? length : 0;
}

void settings_write_reminders(const void *data, size_t length)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_blob(handle, NVS_REMINDERS, data, length);
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
