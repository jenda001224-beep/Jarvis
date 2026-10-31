#include "cloud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "cJSON.h"
#include "cloudmail.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "net.h"
#include "settings.h"
#include "web.h"

#define CONTAINER "iCloud.com.jenda.jarvis"
#define DEFAULT_SERVER "https://api.apple-cloudkit.com"
#define SUBPATH "/database/1/" CONTAINER "/development/public/records/"
#define RECORD_MAIL "JarvisMail"
#define RECORD_NOTICE "JarvisNotice"
#define RESPONSE_LIMIT (256 * 1024)
#define SLOW_POLL_MS (20 * 1000)
#define FAST_POLL_MS (3 * 1000)
#define FAST_WINDOW_MS (2 * 60 * 1000)
#define SNAPSHOT_REFRESH_MS (10 * 60 * 1000)
#define SNAPSHOT_CHECK_MS (5 * 1000)
#define ERROR_WAIT_MS (60 * 1000)
#define NONCE_SIZE 12
#define TAG_SIZE 16
#define KEY_SIZE 32
#define PUSH_QUEUE 8
#define PUSH_TEXT 200

typedef struct {
    bool used;
    char phone[17];
    uint8_t key[KEY_SIZE];
    int64_t seen;
    bool primed;
    int64_t reply_seq;
    uint32_t snapshot_mark;
    int64_t snapshot_at;
} phone_t;

static const char *TAG = "cloud";
static phone_t phones[API_TOKENS];
static cloud_tokens_t read_tokens;
static QueueHandle_t pushes;
static volatile bool idle_now = true;
static mbedtls_pk_context private_key;
static char problem[96];
static int64_t fast_until;

static int64_t clock_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int fill_random(void *context, unsigned char *out, size_t length)
{
    (void) context;
    esp_fill_random(out, length);
    return 0;
}

bool cloud_configured(void)
{
    const settings_t *settings = settings_get();
    return settings->cloud_key_id[0] && settings->cloud_private_key[0];
}

const char *cloud_problem(void)
{
    return problem;
}

void cloud_set_idle(bool idle)
{
    idle_now = idle;
}

void cloud_push(const char *text)
{
    if (!pushes) {
        return;
    }
    char *copy = strdup(text);
    if (copy && xQueueSend(pushes, &copy, 0) != pdTRUE) {
        free(copy);
    }
}

static bool to_base64(const uint8_t *data, size_t length, char *out, size_t capacity)
{
    size_t written = 0;
    if (mbedtls_base64_encode((unsigned char *) out, capacity, &written, data, length) != 0) {
        return false;
    }
    out[written] = 0;
    return true;
}

static char *signed_post(const char *operation, const char *body, size_t *length, int *status)
{
    const settings_t *settings = settings_get();
    char subpath[128];
    snprintf(subpath, sizeof(subpath), SUBPATH "%s", operation);
    char url[192];
    snprintf(url, sizeof(url), "%s%s", settings->cloud_server[0] ? settings->cloud_server : DEFAULT_SERVER, subpath);

    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);

    uint8_t digest[32];
    mbedtls_sha256((const unsigned char *) body, strlen(body), digest, 0);
    char body_hash[64];
    to_base64(digest, sizeof(digest), body_hash, sizeof(body_hash));

    char message[256];
    cloud_signature_message(date, body_hash, subpath, message, sizeof(message));
    mbedtls_sha256((const unsigned char *) message, strlen(message), digest, 0);
    uint8_t signature[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t signature_size = 0;
    if (mbedtls_pk_sign(&private_key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, sizeof(signature), &signature_size, fill_random,
                        NULL) != 0) {
        strlcpy(problem, "Podpis se nepovedl, zkontroluj klíč.", sizeof(problem));
        return NULL;
    }
    char signature_text[256];
    to_base64(signature, signature_size, signature_text, sizeof(signature_text));
    const web_header_t headers[] = {
        {"Content-Type", "text/plain"},
        {"X-Apple-CloudKit-Request-KeyID", settings->cloud_key_id},
        {"X-Apple-CloudKit-Request-ISO8601Date", date},
        {"X-Apple-CloudKit-Request-SignatureV1", signature_text},
    };
    const web_request_t request = {
        .url = url,
        .method = WEB_POST,
        .headers = headers,
        .header_count = 4,
        .body = body,
        .limit = RESPONSE_LIMIT,
        .timeout_ms = 20000,
    };
    return web_request(&request, status, length);
}

static char *seal(const phone_t *phone, const char *plain)
{
    size_t plain_size = strlen(plain);
    size_t sealed_size = NONCE_SIZE + plain_size + TAG_SIZE;
    uint8_t *sealed = heap_caps_malloc(sealed_size, MALLOC_CAP_SPIRAM);
    size_t text_size = (sealed_size / 3 + 2) * 4 + 1;
    char *text = heap_caps_malloc(text_size, MALLOC_CAP_SPIRAM);
    bool done = false;
    if (sealed && text) {
        esp_fill_random(sealed, NONCE_SIZE);
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        done = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, phone->key, KEY_SIZE * 8) == 0 &&
               mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain_size, sealed, NONCE_SIZE, NULL, 0, (const unsigned char *) plain,
                                         sealed + NONCE_SIZE, TAG_SIZE, sealed + NONCE_SIZE + plain_size) == 0 &&
               to_base64(sealed, sealed_size, text, text_size);
        mbedtls_gcm_free(&gcm);
    }
    free(sealed);
    if (!done) {
        free(text);
        return NULL;
    }
    return text;
}

static char *unseal(const phone_t *phone, const char *text)
{
    size_t text_size = strlen(text);
    size_t capacity = text_size / 4 * 3 + 4;
    uint8_t *sealed = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM);
    char *plain = heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM);
    size_t sealed_size = 0;
    bool done = false;
    if (sealed && plain && mbedtls_base64_decode(sealed, capacity, &sealed_size, (const unsigned char *) text, text_size) == 0 &&
        sealed_size >= NONCE_SIZE + TAG_SIZE) {
        size_t plain_size = sealed_size - NONCE_SIZE - TAG_SIZE;
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        done = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, phone->key, KEY_SIZE * 8) == 0 &&
               mbedtls_gcm_auth_decrypt(&gcm, plain_size, sealed, NONCE_SIZE, NULL, 0, sealed + sealed_size - TAG_SIZE, TAG_SIZE,
                                        sealed + NONCE_SIZE, (unsigned char *) plain) == 0;
        mbedtls_gcm_free(&gcm);
        plain[plain_size] = 0;
    }
    free(sealed);
    if (!done) {
        free(plain);
        return NULL;
    }
    return plain;
}

static bool store_record(const char *name, const char *type, int64_t seq, const char *data, const char *text)
{
    char *body = cloud_modify_body(name ? "forceReplace" : "create", name, type, seq, data, text);
    if (!body) {
        return false;
    }
    int status = 0;
    size_t length = 0;
    char *answer = signed_post("modify", body, &length, &status);
    if (answer && status == 200 && strstr(answer, "NOT_FOUND")) {
        free(answer);
        answer = NULL;
        free(body);
        body = cloud_modify_body("create", name, type, seq, data, text);
        answer = body ? signed_post("modify", body, &length, &status) : NULL;
    }
    bool stored = answer && status == 200 && !strstr(answer, "serverErrorCode");
    if (!stored) {
        ESP_LOGW(TAG, "storing %s failed with %d: %.160s", name ? name : type, status, answer ? answer : "");
    }
    free(answer);
    free(body);
    return stored;
}

static api_method_t method_named(const char *name)
{
    return strcmp(name, "POST") == 0 ? API_POST : strcmp(name, "PUT") == 0 ? API_PUT : strcmp(name, "PATCH") == 0 ? API_PATCH : strcmp(name, "DELETE") == 0 ? API_DELETE : API_GET;
}

static void run_command(phone_t *phone, int64_t seq, const char *plain)
{
    cJSON *command = cJSON_Parse(plain);
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(command, "method");
    const cJSON *path = cJSON_GetObjectItemCaseSensitive(command, "path");
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(command, "body");
    api_call_t call = {.status = 200, .authorized = true};
    if (cJSON_IsString(method) && cJSON_IsString(path) && strlen(path->valuestring) < sizeof(call.path)) {
        call.method = method_named(method->valuestring);
        strlcpy(call.path, path->valuestring, sizeof(call.path));
        call.body = cJSON_IsString(body) ? body->valuestring : NULL;
        ESP_LOGI(TAG, "remote %s %s", method->valuestring, call.path);
        if (!api_call_inside(&call)) {
            call.status = 503;
        }
    } else {
        call.status = 400;
    }
    cJSON *answer = cJSON_CreateObject();
    cJSON_AddNumberToObject(answer, "status", call.status);
    if (call.reply) {
        cJSON_AddStringToObject(answer, "reply", call.reply);
    }
    char *plain_answer = cJSON_PrintUnformatted(answer);
    cJSON_Delete(answer);
    cJSON_Delete(command);
    free(call.reply);
    char *data = plain_answer ? seal(phone, plain_answer) : NULL;
    free(plain_answer);
    if (data) {
        char name[32];
        snprintf(name, sizeof(name), "rep-%s", phone->phone);
        store_record(name, RECORD_MAIL, seq, data, NULL);
        free(data);
    }
}

static void poll_mail(void)
{
    const char *names[API_TOKENS];
    char storage[API_TOKENS][32];
    int indexes[API_TOKENS];
    int count = 0;
    for (int i = 0; i < API_TOKENS; i++) {
        if (phones[i].used) {
            snprintf(storage[count], sizeof(storage[0]), "cmd-%s", phones[i].phone);
            names[count] = storage[count];
            indexes[count] = i;
            count++;
        }
    }
    if (count == 0) {
        return;
    }
    char *body = cloud_lookup_body(names, count);
    int status = 0;
    size_t length = 0;
    char *answer = body ? signed_post("lookup", body, &length, &status) : NULL;
    free(body);
    if (!answer || status != 200) {
        if (status == 401 || status == 403) {
            strlcpy(problem, "iCloud odmítl klíč, zkontroluj Key ID.", sizeof(problem));
        } else {
            snprintf(problem, sizeof(problem), "iCloud odpověděl %d.", status);
        }
        ESP_LOGW(TAG, "lookup failed with %d: %.200s", status, answer ? answer : "");
        free(answer);
        fast_until = 0;
        vTaskDelay(pdMS_TO_TICKS(ERROR_WAIT_MS));
        return;
    }
    problem[0] = 0;
    cJSON *root = cJSON_ParseWithLength(answer, length);
    free(answer);
    const cJSON *record;
    cJSON_ArrayForEach(record, cJSON_GetObjectItemCaseSensitive(root, "records"))
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(record, "recordName");
        const cJSON *fields = cJSON_GetObjectItemCaseSensitive(record, "fields");
        if (!cJSON_IsString(name)) {
            continue;
        }
        for (int k = 0; k < count; k++) {
            phone_t *phone = &phones[indexes[k]];
            if (strcmp(names[k], name->valuestring) != 0) {
                continue;
            }
            const cJSON *seq_field = fields ? cJSON_GetObjectItemCaseSensitive(fields, "seq") : NULL;
            const cJSON *data_field = fields ? cJSON_GetObjectItemCaseSensitive(fields, "data") : NULL;
            const cJSON *seq = seq_field ? cJSON_GetObjectItemCaseSensitive(seq_field, "value") : NULL;
            const cJSON *data = data_field ? cJSON_GetObjectItemCaseSensitive(data_field, "value") : NULL;
            if (!cJSON_IsNumber(seq) || !cJSON_IsString(data)) {
                phone->primed = true;
                break;
            }
            int64_t value = (int64_t) seq->valuedouble;
            if (!phone->primed) {
                phone->primed = true;
                phone->seen = value;
                break;
            }
            if (value != phone->seen) {
                phone->seen = value;
                char *plain = unseal(phone, data->valuestring);
                if (plain) {
                    fast_until = clock_ms() + FAST_WINDOW_MS;
                    run_command(phone, value, plain);
                    free(plain);
                } else {
                    ESP_LOGW(TAG, "a command did not decrypt");
                }
            }
            break;
        }
    }
    for (int k = 0; k < count; k++) {
        phones[indexes[k]].primed = true;
    }
    cJSON_Delete(root);
}

static void send_pushes(void)
{
    char *text;
    while (xQueueReceive(pushes, &text, 0) == pdTRUE) {
        store_record(NULL, RECORD_NOTICE, (int64_t) time(NULL), NULL, text);
        free(text);
    }
}

static void send_snapshots(int64_t now, bool due)
{
    static int64_t last;
    if (now - last < SNAPSHOT_CHECK_MS && !due) {
        return;
    }
    last = now;
    api_call_t call = {.status = 200, .authorized = true, .method = API_GET};
    strlcpy(call.path, "/api/snapshot", sizeof(call.path));
    if (!api_call_inside(&call) || !call.reply) {
        free(call.reply);
        return;
    }
    cJSON *root = cJSON_Parse(call.reply);
    const cJSON *mark = cJSON_GetObjectItemCaseSensitive(root, "mark");
    uint32_t value = cJSON_IsNumber(mark) ? (uint32_t) mark->valuedouble : 0;
    cJSON_Delete(root);
    for (int i = 0; i < API_TOKENS; i++) {
        phone_t *phone = &phones[i];
        if (!phone->used || (phone->snapshot_mark == value && now - phone->snapshot_at < SNAPSHOT_REFRESH_MS)) {
            continue;
        }
        char *data = seal(phone, call.reply);
        if (data) {
            char name[32];
            snprintf(name, sizeof(name), "snap-%s", phone->phone);
            if (store_record(name, RECORD_MAIL, (int64_t) time(NULL), data, NULL)) {
                phone->snapshot_mark = value;
                phone->snapshot_at = now;
            }
            free(data);
        }
    }
    free(call.reply);
}

static void load_phones(void)
{
    static char tokens[API_TOKENS][API_TOKEN_LENGTH + 1];
    read_tokens(tokens);
    for (int i = 0; i < API_TOKENS; i++) {
        phone_t *phone = &phones[i];
        bool used = tokens[i][0] != 0;
        if (!used) {
            phone->used = false;
            continue;
        }
        uint8_t digest[32];
        mbedtls_sha256((const unsigned char *) tokens[i], strlen(tokens[i]), digest, 0);
        char phone_name[17];
        for (int k = 0; k < 8; k++) {
            snprintf(phone_name + k * 2, 3, "%02x", digest[k]);
        }
        if (phone->used && strcmp(phone->phone, phone_name) == 0) {
            continue;
        }
        memset(phone, 0, sizeof(*phone));
        phone->used = true;
        strlcpy(phone->phone, phone_name, sizeof(phone->phone));
        char seed[API_TOKEN_LENGTH + 16];
        int size = snprintf(seed, sizeof(seed), "jarvis-cloud%s", tokens[i]);
        mbedtls_sha256((const unsigned char *) seed, (size_t) size, phone->key, 0);
    }
}

static void cloud_task(void *arg)
{
    (void) arg;
    int64_t last_mail = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!net_connected() || !net_time_valid()) {
            continue;
        }
        int64_t now = clock_ms();
        send_pushes();
        if (!idle_now) {
            continue;
        }
        bool fast = now < fast_until;
        if (now - last_mail >= (fast ? FAST_POLL_MS : SLOW_POLL_MS)) {
            last_mail = now;
            load_phones();
            poll_mail();
            send_snapshots(clock_ms(), fast);
        }
    }
}

void cloud_start(cloud_tokens_t tokens)
{
    if (!cloud_configured()) {
        return;
    }
    read_tokens = tokens;
    mbedtls_pk_init(&private_key);
    const char *pem = settings_get()->cloud_private_key;
    if (mbedtls_pk_parse_key(&private_key, (const unsigned char *) pem, strlen(pem) + 1, NULL, 0, fill_random, NULL) != 0) {
        strlcpy(problem, "Klíč pro iCloud se nepodařilo načíst.", sizeof(problem));
        ESP_LOGE(TAG, "%s", problem);
        return;
    }
    pushes = xQueueCreate(PUSH_QUEUE, sizeof(char *));
    xTaskCreatePinnedToCoreWithCaps(cloud_task, "cloud", 16384, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "cloud mailbox started");
}
