#include "api.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"

#define BODY_LIMIT 4096
#define REPLY_WAIT_MS 5000
#define SERVER_STACK 6144
#define URI_HANDLERS 6

static const char *TAG = "api";
static httpd_handle_t server;
static SemaphoreHandle_t token_lock;
static SemaphoreHandle_t call_lock;
static SemaphoreHandle_t call_done;
static SemaphoreHandle_t serve_lock;
static api_call_t *pending;
static char tokens[API_TOKENS][API_TOKEN_LENGTH + 1];

static bool same_secret(const char *a, const char *b)
{
    unsigned char difference = 0;
    for (int i = 0; i < API_TOKEN_LENGTH; i++) {
        difference |= (unsigned char) a[i] ^ (unsigned char) b[i];
    }
    return difference == 0;
}

static bool known_token(const char *token)
{
    if (strlen(token) != API_TOKEN_LENGTH) {
        return false;
    }
    bool known = false;
    xSemaphoreTake(token_lock, portMAX_DELAY);
    for (int i = 0; i < API_TOKENS; i++) {
        known |= tokens[i][0] && same_secret(tokens[i], token);
    }
    xSemaphoreGive(token_lock);
    return known;
}

void api_set_tokens(const char list[API_TOKENS][API_TOKEN_LENGTH + 1])
{
    xSemaphoreTake(token_lock, portMAX_DELAY);
    memcpy(tokens, list, sizeof(tokens));
    xSemaphoreGive(token_lock);
}

static api_method_t method_of(int method)
{
    switch (method) {
    case HTTP_POST:
        return API_POST;
    case HTTP_PUT:
        return API_PUT;
    case HTTP_PATCH:
        return API_PATCH;
    case HTTP_DELETE:
        return API_DELETE;
    default:
        return API_GET;
    }
}

static esp_err_t respond(httpd_req_t *request, int status, const char *body)
{
    char line[32];
    snprintf(line, sizeof(line), "%d %s", status,
             status == 200   ? "OK"
             : status == 400 ? "Bad Request"
             : status == 401 ? "Unauthorized"
             : status == 404 ? "Not Found"
             : status == 413 ? "Payload Too Large"
             : status == 429 ? "Too Many Requests"
                             : "Service Unavailable");
    httpd_resp_set_status(request, line);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body ? body : "{}");
}

static esp_err_t handle(httpd_req_t *request)
{
    api_call_t call = {.method = method_of(request->method)};
    const char *query = strchr(request->uri, '?');
    size_t path_length = query ? (size_t) (query - request->uri) : strlen(request->uri);
    if (path_length >= sizeof(call.path)) {
        return respond(request, 404, "{\"error\":\"not found\"}");
    }
    memcpy(call.path, request->uri, path_length);
    char authorization[16 + API_TOKEN_LENGTH];
    if (httpd_req_get_hdr_value_str(request, "Authorization", authorization, sizeof(authorization)) == ESP_OK &&
        strncmp(authorization, "Bearer ", 7) == 0) {
        strlcpy(call.token, authorization + 7, sizeof(call.token));
        call.authorized = known_token(call.token);
    }
    if (request->content_len > BODY_LIMIT) {
        return respond(request, 413, "{\"error\":\"too large\"}");
    }
    if (request->content_len > 0) {
        call.body = heap_caps_calloc(1, request->content_len + 1, MALLOC_CAP_SPIRAM);
        if (!call.body) {
            return respond(request, 503, "{\"error\":\"busy\"}");
        }
        size_t received = 0;
        while (received < request->content_len) {
            int count = httpd_req_recv(request, call.body + received, request->content_len - received);
            if (count <= 0) {
                free(call.body);
                return ESP_FAIL;
            }
            received += (size_t) count;
        }
    }
    xSemaphoreTake(call_lock, portMAX_DELAY);
    xSemaphoreTake(call_done, 0);
    pending = &call;
    app_post(APP_EVENT_API);
    bool answered = xSemaphoreTake(call_done, pdMS_TO_TICKS(REPLY_WAIT_MS)) == pdTRUE;
    if (!answered) {
        xSemaphoreTake(serve_lock, portMAX_DELAY);
        answered = xSemaphoreTake(call_done, 0) == pdTRUE;
        pending = NULL;
        xSemaphoreGive(serve_lock);
    }
    xSemaphoreGive(call_lock);
    free(call.body);
    esp_err_t result = answered ? respond(request, call.status ? call.status : 200, call.reply)
                                : respond(request, 503, "{\"error\":\"busy\"}");
    free(call.reply);
    return result;
}

void api_serve(api_handler_t handler)
{
    xSemaphoreTake(serve_lock, portMAX_DELAY);
    api_call_t *call = pending;
    if (call) {
        handler(call);
        pending = NULL;
        xSemaphoreGive(call_done);
    }
    xSemaphoreGive(serve_lock);
}

void api_start(const char *version)
{
    token_lock = xSemaphoreCreateMutex();
    call_lock = xSemaphoreCreateMutex();
    call_done = xSemaphoreCreateBinary();
    serve_lock = xSemaphoreCreateMutex();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = SERVER_STACK;
    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    config.max_uri_handlers = URI_HANDLERS;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "server did not start");
        return;
    }
    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE};
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        const httpd_uri_t uri = {.uri = "/api/*", .method = methods[i], .handler = handle};
        httpd_register_uri_handler(server, &uri);
    }
    mdns_txt_item_t txt[] = {{"model", "Waveshare ESP32-S3-Touch-LCD-4B"}, {"version", version}};
    mdns_service_add("J-WALL Display", "_jarvis", "_tcp", 80, txt, sizeof(txt) / sizeof(txt[0]));
    ESP_LOGI(TAG, "listening on port 80");
}
