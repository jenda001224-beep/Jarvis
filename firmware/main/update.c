#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md5.h"
#include "net.h"
#include "updateinfo.h"
#include "watch.h"
#include "web.h"

#define SITE "https://jenda001224-beep.github.io/Jarvis/"
#define MANIFEST_LIMIT (16 * 1024)
#define APP_OFFSET 0x10000
#define FIRST_CHECK_MS (2 * 60 * 1000)
#define CHECK_EVERY_MS (24 * 60 * 60 * 1000)
#define RETRY_CHECK_MS (30 * 60 * 1000)
#define INSTALL_STACK 12288
#define URL_SIZE 256
#define HEALTHY_AFTER_MS (45 * 1000)
#define GIVE_UP_AFTER_MS (5 * 60 * 1000)

typedef struct {
    esp_ota_handle_t handle;
    mbedtls_md5_context md5;
    size_t written;
    bool failed;
} install_t;

static const char *TAG = "update";
static SemaphoreHandle_t lock;
static TaskHandle_t checker;
static release_t latest;
static volatile bool checked;
static char told_about[RELEASE_VERSION];
static volatile update_phase_t phase;
static volatile int percent;
static char problem[96];
static char *address;
static char expected_md5[RELEASE_MD5];
static size_t expected_size;
static volatile bool requested;
static int64_t online_since;
static bool verifying;

static int64_t clock_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static const char *current_version(void)
{
    return esp_app_get_description()->version;
}

bool update_available(void)
{
    return checked && latest.version[0] && release_compare(latest.version, current_version()) > 0;
}

static bool check_now(void)
{
    size_t length = 0;
    char *body = web_get(SITE "manifest.json", MANIFEST_LIMIT, &length);
    release_t found;
    bool parsed = body && release_parse(body, length, APP_OFFSET, &found);
    free(body);
    if (!parsed) {
        ESP_LOGW(TAG, "the release list could not be read");
        return false;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    latest = found;
    checked = true;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "this is %s, the newest is %s", current_version(), found.version);
    if (update_available() && strcmp(told_about, found.version) != 0) {
        strlcpy(told_about, found.version, sizeof(told_about));
        char text[96];
        snprintf(text, sizeof(text), "Je k dispozici nová verze %s. Stačí říct: Jarvisi, aktualizuj se.", found.version);
        watch_notice("systém", text, false);
    }
    return true;
}

static void checker_task(void *arg)
{
    (void) arg;
    uint32_t wait = FIRST_CHECK_MS;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
        if (phase == UPDATE_DOWNLOADING) {
            wait = RETRY_CHECK_MS;
        } else if (!net_connected()) {
            wait = 10 * 1000;
        } else {
            wait = check_now() ? CHECK_EVERY_MS : RETRY_CHECK_MS;
        }
    }
}

static bool on_image(void *context, const char *data, size_t length)
{
    install_t *job = context;
    if (esp_ota_write(job->handle, data, length) != ESP_OK) {
        job->failed = true;
        return false;
    }
    mbedtls_md5_update(&job->md5, (const unsigned char *) data, length);
    job->written += length;
    if (expected_size > 0) {
        int done = (int) ((uint64_t) job->written * 100 / expected_size);
        percent = done > 100 ? 100 : done;
    }
    return true;
}

static void fail_with(const char *message)
{
    strlcpy(problem, message, sizeof(problem));
    ESP_LOGE(TAG, "%s", message);
    phase = UPDATE_FAILED;
}

static void install_task(void *arg)
{
    (void) arg;
    static install_t job;
    memset(&job, 0, sizeof(job));
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target || esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &job.handle) != ESP_OK) {
        fail_with("Aktualizaci se nepodařilo začít.");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "writing %s into %s", address, target->label);
    mbedtls_md5_init(&job.md5);
    mbedtls_md5_starts(&job.md5);
    bool complete = web_stream(address, on_image, &job);
    uint8_t digest[16];
    mbedtls_md5_finish(&job.md5, digest);
    mbedtls_md5_free(&job.md5);
    char text[RELEASE_MD5];
    for (int i = 0; i < 16; i++) {
        snprintf(text + i * 2, 3, "%02x", digest[i]);
    }
    if (!complete || job.failed) {
        esp_ota_abort(job.handle);
        fail_with("Stahování se přerušilo.");
    } else if (expected_md5[0] && strcmp(text, expected_md5) != 0) {
        esp_ota_abort(job.handle);
        fail_with("Stažený soubor nesouhlasí.");
    } else if (esp_ota_end(job.handle) != ESP_OK) {
        fail_with("Stažený soubor není platný.");
    } else if (esp_ota_set_boot_partition(target) != ESP_OK) {
        fail_with("Novou verzi se nepodařilo nastavit.");
    } else {
        ESP_LOGI(TAG, "%u bytes written, restarting into %s", (unsigned) job.written, target->label);
        percent = 100;
        phase = UPDATE_DONE;
    }
    vTaskDelete(NULL);
}

bool update_begin(const char *url, size_t size)
{
    if (phase == UPDATE_DOWNLOADING || !address) {
        return false;
    }
    if (url) {
        strlcpy(address, url, URL_SIZE);
        expected_md5[0] = 0;
        expected_size = size;
    } else if (update_available()) {
        xSemaphoreTake(lock, portMAX_DELAY);
        snprintf(address, URL_SIZE, SITE "%s", latest.path);
        strlcpy(expected_md5, latest.md5, sizeof(expected_md5));
        expected_size = latest.size;
        xSemaphoreGive(lock);
    } else {
        return false;
    }
    problem[0] = 0;
    percent = 0;
    phase = UPDATE_DOWNLOADING;
    if (xTaskCreatePinnedToCore(install_task, "update", INSTALL_STACK, NULL, 3, NULL, 0) != pdPASS) {
        fail_with("Na aktualizaci není dost paměti.");
        return false;
    }
    return true;
}

update_phase_t update_phase(void)
{
    return phase;
}

int update_percent(void)
{
    return percent;
}

void update_clear_failure(void)
{
    if (phase == UPDATE_FAILED) {
        phase = UPDATE_IDLE;
    }
}

void update_check_soon(void)
{
    if (checker) {
        xTaskNotifyGive(checker);
    }
}

void update_tick(bool online)
{
    if (!verifying) {
        return;
    }
    int64_t now = clock_ms();
    if (!online) {
        online_since = 0;
    } else if (!online_since) {
        online_since = now;
    }
    if (online_since && now - online_since >= HEALTHY_AFTER_MS) {
        verifying = false;
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "this version is healthy, keeping it");
    } else if (now >= GIVE_UP_AFTER_MS) {
        ESP_LOGE(TAG, "no network since the update, going back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

bool update_take_request(void)
{
    bool wanted = requested;
    requested = false;
    return wanted;
}

char *update_tool(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const cJSON *install = cJSON_GetObjectItemCaseSensitive(args, "install");
    bool wants_install = cJSON_IsTrue(install);
    cJSON_Delete(args);
    bool known = check_now();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "current", current_version());
    if (known) {
        cJSON_AddStringToObject(root, "newest", latest.version);
    }
    if (!known) {
        cJSON_AddStringToObject(root, "result", "Seznam verzí se nepodařilo načíst.");
    } else if (!update_available()) {
        cJSON_AddStringToObject(root, "result", "Běží nejnovější verze.");
    } else if (wants_install) {
        requested = true;
        cJSON_AddStringToObject(root, "result", "Aktualizace začne, jakmile domluvíš. Displej se pak restartuje.");
    } else {
        cJSON_AddStringToObject(root, "result", "Je k dispozici novější verze.");
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

cJSON *update_json(void)
{
    static const char *const PHASES[] = {"idle", "downloading", "done", "failed"};
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "current", current_version());
    cJSON_AddStringToObject(root, "newest", latest.version);
    cJSON_AddBoolToObject(root, "checked", checked);
    cJSON_AddBoolToObject(root, "available", update_available());
    cJSON_AddStringToObject(root, "phase", PHASES[phase]);
    cJSON_AddNumberToObject(root, "percent", percent);
    cJSON_AddStringToObject(root, "problem", problem);
    const esp_partition_t *running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "partition", running ? running->label : "");
    return root;
}

void update_start(void)
{
    lock = xSemaphoreCreateMutex();
    address = heap_caps_malloc(URL_SIZE, MALLOC_CAP_SPIRAM);
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        verifying = true;
        ESP_LOGW(TAG, "first start of a new version in %s", running->label);
    }
    if (lock && address) {
        xTaskCreatePinnedToCoreWithCaps(checker_task, "updates", 12288, NULL, 1, &checker, 0, MALLOC_CAP_SPIRAM);
    }
}
