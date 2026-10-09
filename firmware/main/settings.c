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

static const char *TAG = "settings";

static settings_t settings = {
    .model = "gemini-3.8-live",
    .voice = "Charon",
    .place = "Praha",
    .volume = 70,
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
    copy_string(settings.model, sizeof(settings.model), root, "model");
    copy_string(settings.voice, sizeof(settings.voice), root, "voice");
    copy_string(settings.place, sizeof(settings.place), root, "place");
    const cJSON *volume = cJSON_GetObjectItemCaseSensitive(root, "volume");
    if (cJSON_IsNumber(volume) && volume->valueint >= 10 && volume->valueint <= 100) {
        settings.volume = volume->valueint;
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
