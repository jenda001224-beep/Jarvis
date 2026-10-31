#include "web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define CHUNK 2048
#define REDIRECT_LIMIT 4
#define TIMEOUT_MS 12000

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    size_t limit;
} body_t;

static const char *TAG = "web";

static bool is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static void user_agent(char *out, size_t capacity)
{
    snprintf(out, capacity, "Jarvis-wall-assistant/%s (https://github.com/jenda001224-beep/Jarvis)",
             esp_app_get_description()->version);
}

bool web_stream(const char *url, web_sink_t sink, void *context)
{
    return web_stream_from(url, 0, sink, context);
}

bool web_stream_from(const char *url, size_t offset, web_sink_t sink, void *context)
{
    char agent[128];
    user_agent(agent, sizeof(agent));
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .user_agent = agent,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *chunk = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    bool complete = false;
    size_t skip = 0;
    if (client && offset > 0) {
        char range[32];
        snprintf(range, sizeof(range), "bytes=%u-", (unsigned) offset);
        esp_http_client_set_header(client, "Range", range);
    }
    for (int hop = 0; client && chunk && hop <= REDIRECT_LIMIT; hop++) {
        if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0) {
            ESP_LOGW(TAG, "no answer from the server");
            break;
        }
        int status = esp_http_client_get_status_code(client);
        if (is_redirect(status)) {
            if (esp_http_client_set_redirection(client) != ESP_OK) {
                break;
            }
            esp_http_client_close(client);
            continue;
        }
        if (status != 200 && !(status == 206 && offset > 0)) {
            ESP_LOGW(TAG, "server answered %d", status);
            break;
        }
        skip = status == 200 ? offset : 0;
        for (;;) {
            int received = esp_http_client_read(client, chunk, CHUNK);
            if (received < 0) {
                break;
            }
            if (received == 0) {
                complete = esp_http_client_is_complete_data_received(client);
                break;
            }
            size_t dropped = skip < (size_t) received ? skip : (size_t) received;
            skip -= dropped;
            if ((size_t) received > dropped && !sink(context, chunk + dropped, (size_t) received - dropped)) {
                break;
            }
        }
        break;
    }
    free(chunk);
    if (client) {
        esp_http_client_cleanup(client);
    }
    return complete;
}

static bool collect(void *context, const char *data, size_t length)
{
    body_t *body = context;
    size_t needed = body->length + length + 1;
    if (needed > body->limit) {
        return false;
    }
    if (needed > body->capacity) {
        size_t capacity = body->capacity ? body->capacity : 2048;
        while (capacity < needed) {
            capacity *= 2;
        }
        char *grown = heap_caps_realloc(body->data, capacity, MALLOC_CAP_SPIRAM);
        if (!grown) {
            return false;
        }
        body->data = grown;
        body->capacity = capacity;
    }
    memcpy(body->data + body->length, data, length);
    body->length += length;
    body->data[body->length] = 0;
    return true;
}

char *web_get(const char *url, size_t limit, size_t *length)
{
    body_t body = {.limit = limit};
    if (!web_stream(url, collect, &body) || !body.data) {
        free(body.data);
        return NULL;
    }
    if (length) {
        *length = body.length;
    }
    return body.data;
}

static const esp_http_client_method_t METHODS[] = {
    [WEB_GET] = HTTP_METHOD_GET,
    [WEB_POST] = HTTP_METHOD_POST,
    [WEB_PUT] = HTTP_METHOD_PUT,
    [WEB_DELETE] = HTTP_METHOD_DELETE,
    [WEB_PROPFIND] = HTTP_METHOD_PROPFIND,
    [WEB_REPORT] = HTTP_METHOD_REPORT,
};

char *web_request(const web_request_t *request, int *status, size_t *length)
{
    char agent[128];
    user_agent(agent, sizeof(agent));
    const esp_http_client_config_t config = {
        .url = request->url,
        .method = METHODS[request->method],
        .timeout_ms = request->timeout_ms ? request->timeout_ms : TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .user_agent = agent,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    *status = 0;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *chunk = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    body_t received = {.limit = request->limit};
    bool complete = false;
    int body_length = request->body ? (int) strlen(request->body) : 0;
    if (client && chunk) {
        for (int i = 0; i < request->header_count; i++) {
            esp_http_client_set_header(client, request->headers[i].name, request->headers[i].value);
        }
        if (esp_http_client_open(client, body_length) == ESP_OK &&
            (body_length == 0 || esp_http_client_write(client, request->body, body_length) == body_length) &&
            esp_http_client_fetch_headers(client) >= 0) {
            *status = esp_http_client_get_status_code(client);
            for (;;) {
                int count = esp_http_client_read(client, chunk, CHUNK);
                if (count <= 0) {
                    complete = count == 0 && esp_http_client_is_complete_data_received(client);
                    break;
                }
                bool kept = request->sink ? request->sink(request->context, chunk, (size_t) count)
                                          : collect(&received, chunk, (size_t) count);
                if (!kept) {
                    break;
                }
            }
        } else {
            ESP_LOGW(TAG, "no answer from the server");
        }
    }
    free(chunk);
    if (client) {
        esp_http_client_cleanup(client);
    }
    if (!complete) {
        free(received.data);
        if (*status >= 200 && *status < 300) {
            *status = 0;
        }
        return NULL;
    }
    if (length) {
        *length = received.length;
    }
    return received.data;
}

char *web_post(const char *url, const char *authorization, const char *body, size_t limit, int *status, size_t *length)
{
    const web_header_t headers[] = {{"Content-Type", "application/json"}, {"Authorization", authorization}};
    const web_request_t request = {
        .url = url,
        .method = WEB_POST,
        .headers = headers,
        .header_count = 2,
        .body = body,
        .limit = limit,
    };
    char *answer = web_request(&request, status, length);
    if (answer && *status != 200) {
        free(answer);
        return NULL;
    }
    return answer;
}
