#include "printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bambu.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "memory.h"
#include "mqtt_client.h"
#include "net.h"
#include "settings.h"
#include "web.h"

#define MESSAGE_LIMIT (128 * 1024)
#define TEXT_SLOTS 6
#define LOOKUP_RETRY_MS 60000
#define LOOKUP_WAIT_MS 4000
#define SEARCH_ROUND_MS 20000
#define SEARCH_PAUSE_MS 20000
#define LOST_AFTER_MS 120000
#define PACKET_SIZE 1024
#define SERVICE "urn:bambulab-com:device:3dprinter:1"
#define STATUS_REQUEST "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}"

typedef struct {
    bambu_text_t entry;
    int64_t tried_at;
} lookup_t;

static const char *TAG = "printer";
static const uint16_t ANNOUNCE_PORTS[] = {2021, 1990};

static char address[40];
static char serial[24];
static char report_topic[48];
static char request_topic[48];
static char broker[64];
static char client_name[24];
static esp_mqtt_client_handle_t client;
static SemaphoreHandle_t lock;
static bambu_status_t status;
static EXTERNAL_RAM lookup_t lookups[TEXT_SLOTS];
static int next_slot;
static char *incoming;
static size_t incoming_capacity;
static size_t incoming_length;
static bool incoming_broken;
static volatile bool connected;
static volatile bool refused;
static volatile bool found;
static volatile int64_t last_seen;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

bool printer_configured(void)
{
    return settings_get()->printer_code[0] != 0;
}

static void on_message(const esp_mqtt_event_t *event)
{
    if (event->current_data_offset == 0) {
        incoming_length = 0;
        incoming_broken = event->total_data_len <= 0 || event->total_data_len > MESSAGE_LIMIT;
        if (!incoming_broken && (size_t) event->total_data_len + 1 > incoming_capacity) {
            char *grown = heap_caps_realloc(incoming, (size_t) event->total_data_len + 1, MALLOC_CAP_SPIRAM);
            if (grown) {
                incoming = grown;
                incoming_capacity = (size_t) event->total_data_len + 1;
            } else {
                incoming_broken = true;
            }
        }
    }
    if (incoming_broken || event->data_len <= 0 || (size_t) event->current_data_offset != incoming_length ||
        incoming_length + (size_t) event->data_len >= incoming_capacity) {
        incoming_broken = true;
        return;
    }
    memcpy(incoming + incoming_length, event->data, (size_t) event->data_len);
    incoming_length += (size_t) event->data_len;
    if (incoming_length < (size_t) event->total_data_len) {
        return;
    }
    incoming[incoming_length] = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    bambu_merge_report(&status, incoming, incoming_length);
    xSemaphoreGive(lock);
    last_seen = now_ms();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const esp_mqtt_event_t *event = data;
    switch ((esp_mqtt_event_id_t) id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        connected = true;
        refused = false;
        last_seen = now_ms();
        esp_mqtt_client_subscribe(event->client, report_topic, 0);
        esp_mqtt_client_publish(event->client, request_topic, STATUS_REQUEST, 0, 0, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (connected) {
            ESP_LOGW(TAG, "connection lost");
        }
        connected = false;
        break;
    case MQTT_EVENT_DATA:
        on_message(event);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            if (!refused) {
                ESP_LOGW(TAG, "printer refused the access code");
            }
            refused = true;
        }
        break;
    default:
        break;
    }
}

static int open_listener(uint16_t port)
{
    int handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle < 0) {
        return -1;
    }
    int enabled = 1;
    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    setsockopt(handle, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(handle, (struct sockaddr *) &local, sizeof(local)) != 0) {
        close(handle);
        return -1;
    }
    return handle;
}

static void ask_printers(int handle)
{
    static const char QUESTION[] = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                                   "MX: 3\r\nST: " SERVICE "\r\n\r\n";
    struct sockaddr_in everyone = {.sin_family = AF_INET, .sin_port = htons(1900)};
    everyone.sin_addr.s_addr = inet_addr("239.255.255.250");
    sendto(handle, QUESTION, sizeof(QUESTION) - 1, 0, (struct sockaddr *) &everyone, sizeof(everyone));
}

static bool accept_announcement(const char *packet, size_t length, const struct sockaddr_in *source)
{
    const settings_t *settings = settings_get();
    char seen_address[sizeof(address)];
    char seen_serial[sizeof(serial)];
    if (!bambu_parse_announcement(packet, length, seen_address, sizeof(seen_address), seen_serial,
                                  sizeof(seen_serial))) {
        return false;
    }
    if (!seen_address[0]) {
        inet_ntoa_r(source->sin_addr, seen_address, sizeof(seen_address));
    }
    if ((settings->printer_serial[0] && strcmp(settings->printer_serial, seen_serial) != 0) ||
        (settings->printer_address[0] && strcmp(settings->printer_address, seen_address) != 0)) {
        return false;
    }
    strcpy(address, seen_address);
    strcpy(serial, seen_serial);
    return true;
}

static bool search(void)
{
    int handles[3] = {open_listener(ANNOUNCE_PORTS[0]), open_listener(ANNOUNCE_PORTS[1]), open_listener(0)};
    char *packet = heap_caps_malloc(PACKET_SIZE, MALLOC_CAP_SPIRAM);
    int64_t deadline = now_ms() + SEARCH_ROUND_MS;
    int64_t asked_at = 0;
    bool located = false;
    while (packet && !located && now_ms() < deadline) {
        if (handles[2] >= 0 && now_ms() - asked_at > 5000) {
            ask_printers(handles[2]);
            asked_at = now_ms();
        }
        fd_set readable;
        FD_ZERO(&readable);
        int highest = -1;
        for (int i = 0; i < 3; i++) {
            if (handles[i] >= 0) {
                FD_SET(handles[i], &readable);
                highest = handles[i] > highest ? handles[i] : highest;
            }
        }
        if (highest < 0) {
            break;
        }
        struct timeval patience = {.tv_sec = 1};
        if (select(highest + 1, &readable, NULL, NULL, &patience) <= 0) {
            continue;
        }
        for (int i = 0; i < 3 && !located; i++) {
            if (handles[i] < 0 || !FD_ISSET(handles[i], &readable)) {
                continue;
            }
            struct sockaddr_in source = {0};
            socklen_t source_length = sizeof(source);
            int received =
                recvfrom(handles[i], packet, PACKET_SIZE - 1, 0, (struct sockaddr *) &source, &source_length);
            located = received > 0 && accept_announcement(packet, (size_t) received, &source);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (handles[i] >= 0) {
            close(handles[i]);
        }
    }
    free(packet);
    return located;
}

static bool open_broker(void)
{
    snprintf(report_topic, sizeof(report_topic), "device/%s/report", serial);
    snprintf(request_topic, sizeof(request_topic), "device/%s/request", serial);
    snprintf(broker, sizeof(broker), "mqtts://%s:8883", address);
    const esp_mqtt_client_config_t config = {
        .broker.address.uri = broker,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.username = "bblp",
        .credentials.client_id = client_name,
        .credentials.authentication.password = settings_get()->printer_code,
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 15000,
        .network.timeout_ms = 8000,
        .buffer.size = 4096,
        .buffer.out_size = 512,
        .task.stack_size = 6144,
        .task.priority = 3,
    };
    client = esp_mqtt_client_init(&config);
    if (!client) {
        return false;
    }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, on_event, NULL);
    last_seen = now_ms();
    return esp_mqtt_client_start(client) == ESP_OK;
}

static void close_broker(void)
{
    if (client) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }
    connected = false;
}

static lookup_t *lookup_for(const char *code)
{
    for (int i = 0; i < TEXT_SLOTS; i++) {
        if (strcmp(lookups[i].entry.code, code) == 0) {
            return &lookups[i];
        }
    }
    return NULL;
}

static int collect_codes(char codes[][BAMBU_CODE_LENGTH])
{
    int count = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (status.print_error) {
        bambu_error_code(status.print_error, codes[count++]);
    }
    for (int i = 0; i < status.alert_count; i++) {
        bambu_alert_code(&status.alerts[i], codes[count++]);
    }
    xSemaphoreGive(lock);
    return count;
}

static bool texts_missing(void)
{
    char codes[BAMBU_MAX_ALERTS + 1][BAMBU_CODE_LENGTH];
    int count = collect_codes(codes);
    bool missing = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < count; i++) {
        const lookup_t *known = lookup_for(codes[i]);
        missing = missing || !known || (!known->entry.text[0] && now_ms() - known->tried_at > LOOKUP_RETRY_MS);
    }
    xSemaphoreGive(lock);
    return missing;
}

static void fetch_texts(void)
{
    char codes[BAMBU_MAX_ALERTS + 1][BAMBU_CODE_LENGTH];
    int count = collect_codes(codes);
    for (int i = 0; i < count; i++) {
        xSemaphoreTake(lock, portMAX_DELAY);
        const lookup_t *known = lookup_for(codes[i]);
        bool wanted = !known || (!known->entry.text[0] && now_ms() - known->tried_at > LOOKUP_RETRY_MS);
        xSemaphoreGive(lock);
        if (!wanted) {
            continue;
        }
        char url[160];
        char text[BAMBU_TEXT_LENGTH] = "";
        snprintf(url, sizeof(url), "https://e.bambulab.com/query.php?lang=en&e=%s", codes[i]);
        size_t length = 0;
        char *body = web_get(url, 16384, &length);
        if (body) {
            bambu_parse_lookup(body, length, text, sizeof(text));
        }
        free(body);
        xSemaphoreTake(lock, portMAX_DELAY);
        lookup_t *slot = lookup_for(codes[i]);
        if (!slot) {
            slot = &lookups[next_slot];
            next_slot = (next_slot + 1) % TEXT_SLOTS;
        }
        strcpy(slot->entry.code, codes[i]);
        strcpy(slot->entry.text, text);
        slot->tried_at = now_ms();
        xSemaphoreGive(lock);
    }
}

static void printer_task(void *arg)
{
    const settings_t *settings = settings_get();
    bool fixed = settings->printer_address[0] && settings->printer_serial[0];
    if (fixed) {
        strlcpy(address, settings->printer_address, sizeof(address));
        strlcpy(serial, settings->printer_serial, sizeof(serial));
    }
    for (;;) {
        if (!net_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!fixed && !search()) {
            found = false;
            vTaskDelay(pdMS_TO_TICKS(SEARCH_PAUSE_MS));
            continue;
        }
        found = true;
        ESP_LOGI(TAG, "printer at %s", address);
        if (!open_broker()) {
            close_broker();
            vTaskDelay(pdMS_TO_TICKS(SEARCH_PAUSE_MS));
            continue;
        }
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (connected && texts_missing()) {
                fetch_texts();
            }
            if (!fixed && !connected && !refused && now_ms() - last_seen > LOST_AFTER_MS) {
                break;
            }
        }
        close_broker();
    }
}

void printer_start(void)
{
    if (!printer_configured()) {
        return;
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(client_name, sizeof(client_name), "jarvis-%02x%02x%02x", mac[3], mac[4], mac[5]);
    bambu_status_reset(&status);
    lock = xSemaphoreCreateMutex();
    if (lock) {
        xTaskCreatePinnedToCoreWithCaps(printer_task, "printer", 12288, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
    }
}

static char *offline(const char *reason)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", false);
    cJSON_AddStringToObject(root, "reason", reason);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *printer_tool_status(void)
{
    if (!lock) {
        return offline("Tiskárna není nastavená.");
    }
    if (refused) {
        return offline("Tiskárna odmítla přístupový kód. Pán ho má opravit v nastavení.");
    }
    if (!found) {
        return offline("Tiskárnu jsem v síti nenašel. Je nejspíš vypnutá.");
    }
    if (!connected) {
        return offline("Tiskárna neodpovídá. Je nejspíš vypnutá nebo se právě připojuje.");
    }
    for (int64_t deadline = now_ms() + LOOKUP_WAIT_MS; texts_missing() && now_ms() < deadline;) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    bambu_status_t snapshot;
    bambu_text_t texts[TEXT_SLOTS];
    xSemaphoreTake(lock, portMAX_DELAY);
    snapshot = status;
    for (int i = 0; i < TEXT_SLOTS; i++) {
        texts[i] = lookups[i].entry;
    }
    xSemaphoreGive(lock);
    return bambu_describe(&snapshot, texts, TEXT_SLOTS);
}

size_t printer_summary(char *out, size_t capacity)
{
    out[0] = 0;
    if (!lock || !connected) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = bambu_summary(&status, out, capacity);
    xSemaphoreGive(lock);
    return length;
}

size_t printer_brief(char *out, size_t capacity)
{
    out[0] = 0;
    if (!lock || !connected) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    size_t length = bambu_brief(&status, out, capacity);
    xSemaphoreGive(lock);
    return length;
}
