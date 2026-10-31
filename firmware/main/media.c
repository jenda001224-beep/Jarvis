#include "media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio.h"
#include "decoder/esp_audio_dec_default.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "memory.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"
#include "web.h"

#define OUTPUT_RATE 24000
#define HOLD_SAMPLES (OUTPUT_RATE * 2)
#define BUFFER_LIMIT_BYTES (AUDIO_SAMPLE_RATE * 2 * 6)
#define NOISE_FLOOR_BYTES (AUDIO_SAMPLE_RATE * 2)
#define NOISE_CHUNK 480
#define FADE_MS (20 * 1000)
#define DEFAULT_NOISE_MINUTES 60
#define LONGEST_NOISE_MINUTES (12 * 60)
#define PCM_BUFFER 16384
#define URL_SIZE 384
#define TITLE_SIZE 64
#define RETRIES 5
#define RETRY_MS 2500
#define RSS_LIMIT (128 * 1024)
#define SEARCH_LIMIT (32 * 1024)

typedef enum {
    KIND_NONE,
    KIND_STREAM,
    KIND_NOISE,
} kind_t;

typedef struct {
    kind_t kind;
    bool resumable;
    noise_kind_t noise;
    int64_t ends_ms;
    char title[TITLE_SIZE];
    char url[URL_SIZE];
} wanted_t;

typedef struct {
    uint32_t generation;
    esp_audio_simple_dec_handle_t decoder;
    uint8_t *pcm;
    uint32_t pcm_size;
    int16_t *converted;
    size_t converted_size;
    rate_t rate;
    uint32_t input_rate;
    int channels;
    bool holding;
    size_t held;
    size_t received;
    int errors;
} stream_t;

static const char *TAG = "media";
static SemaphoreHandle_t lock;
static SemaphoreHandle_t gate;
static TaskHandle_t worker;
static EXTERNAL_RAM wanted_t wanted;
static volatile uint32_t generation;
static volatile bool held;
static volatile bool waiting;
static volatile bool active;
static volatile bool ever_played;
static int16_t *hold_buffer;

static int64_t clock_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool keep_going(uint32_t mine)
{
    return !held && generation == mine;
}

static void hand_over(stream_t *stream, const int16_t *pcm, size_t samples)
{
    xSemaphoreTake(gate, portMAX_DELAY);
    if (keep_going(stream->generation)) {
        if (stream->holding) {
            size_t room = HOLD_SAMPLES - stream->held;
            size_t take = samples < room ? samples : room;
            memcpy(hold_buffer + stream->held, pcm, take * sizeof(int16_t));
            stream->held += take;
            pcm += take;
            samples -= take;
            if (stream->held == HOLD_SAMPLES) {
                audio_play(hold_buffer, stream->held);
                stream->holding = false;
                ever_played = true;
            }
        }
        if (samples > 0 && !stream->holding) {
            audio_play(pcm, samples);
        }
    }
    xSemaphoreGive(gate);
    while (keep_going(stream->generation) && audio_buffered() > BUFFER_LIMIT_BYTES) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void convert_frame(stream_t *stream, uint32_t decoded)
{
    esp_audio_simple_dec_info_t info = {0};
    if (esp_audio_simple_dec_get_info(stream->decoder, &info) != ESP_AUDIO_ERR_OK || info.sample_rate == 0 || info.channel == 0 ||
        info.bits_per_sample != 16) {
        return;
    }
    if (info.sample_rate != stream->input_rate) {
        stream->input_rate = info.sample_rate;
        rate_init(&stream->rate, info.sample_rate, OUTPUT_RATE);
        ESP_LOGI(TAG, "%u Hz, %d channels, %u kbit/s", (unsigned) info.sample_rate, info.channel, (unsigned) (info.bitrate / 1000));
    }
    size_t frames = decoded / (sizeof(int16_t) * info.channel);
    sound_to_mono((int16_t *) stream->pcm, frames, info.channel);
    size_t needed = rate_capacity(&stream->rate, frames);
    if (needed > stream->converted_size) {
        int16_t *grown = heap_caps_realloc(stream->converted, needed * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!grown) {
            return;
        }
        stream->converted = grown;
        stream->converted_size = needed;
    }
    size_t produced = rate_process(&stream->rate, (const int16_t *) stream->pcm, frames, stream->converted, stream->converted_size);
    hand_over(stream, stream->converted, produced);
}

static bool open_decoder(stream_t *stream, const char *data, size_t length)
{
    sound_format_t format = sound_sniff((const uint8_t *) data, length);
    if (format == SOUND_UNKNOWN) {
        return false;
    }
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = format == SOUND_AAC ? ESP_AUDIO_SIMPLE_DEC_TYPE_AAC : ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    if (esp_audio_simple_dec_open(&config, &stream->decoder) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "no decoder");
        return false;
    }
    ESP_LOGI(TAG, "stream is %s", format == SOUND_AAC ? "AAC" : "MP3");
    return true;
}

static bool on_stream(void *context, const char *data, size_t length)
{
    stream_t *stream = context;
    if (!keep_going(stream->generation)) {
        return false;
    }
    if (!stream->decoder && !open_decoder(stream, data, length)) {
        return ++stream->errors < 64;
    }
    stream->received += length;
    esp_audio_simple_dec_raw_t raw = {.buffer = (uint8_t *) data, .len = (uint32_t) length};
    while (raw.len > 0 && keep_going(stream->generation)) {
        esp_audio_simple_dec_out_t out = {.buffer = stream->pcm, .len = stream->pcm_size};
        esp_audio_err_t result = esp_audio_simple_dec_process(stream->decoder, &raw, &out);
        if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            uint8_t *grown = heap_caps_realloc(stream->pcm, out.needed_size, MALLOC_CAP_SPIRAM);
            if (!grown) {
                return false;
            }
            stream->pcm = grown;
            stream->pcm_size = out.needed_size;
            continue;
        }
        if (result != ESP_AUDIO_ERR_OK) {
            if (++stream->errors > 200) {
                ESP_LOGW(TAG, "too many decoding errors");
                return false;
            }
            if (raw.consumed == 0) {
                break;
            }
        } else {
            stream->errors = 0;
        }
        raw.len -= raw.consumed;
        raw.buffer += raw.consumed;
        if (out.decoded_size > 0) {
            convert_frame(stream, out.decoded_size);
        }
    }
    return true;
}

static size_t stream_position;

static bool run_stream(const wanted_t *job, uint32_t mine, int *failures)
{
    stream_t stream = {.generation = mine, .holding = true};
    stream.pcm = heap_caps_malloc(PCM_BUFFER, MALLOC_CAP_SPIRAM);
    stream.pcm_size = PCM_BUFFER;
    if (!stream.pcm) {
        return false;
    }
    size_t start = job->resumable ? stream_position : 0;
    bool complete = web_stream_from(job->url, start, on_stream, &stream);
    if (keep_going(mine)) {
        xSemaphoreTake(gate, portMAX_DELAY);
        if (stream.holding && stream.held > 0) {
            audio_play(hold_buffer, stream.held);
        }
        xSemaphoreGive(gate);
    }
    stream_position = start + stream.received;
    if (stream.received > 64 * 1024) {
        *failures = 0;
    }
    if (stream.decoder) {
        esp_audio_simple_dec_close(stream.decoder);
    }
    free(stream.pcm);
    free(stream.converted);
    return complete;
}

static void run_noise(const wanted_t *job, uint32_t mine)
{
    static EXTERNAL_RAM noise_t noise;
    static EXTERNAL_RAM int16_t chunk[NOISE_CHUNK];
    noise_init(&noise, esp_random());
    int64_t started = clock_ms();
    while (keep_going(mine)) {
        int64_t now = clock_ms();
        if (now >= job->ends_ms) {
            return;
        }
        if (audio_buffered() >= NOISE_FLOOR_BYTES) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        noise_fill(&noise, job->noise, chunk, NOISE_CHUNK);
        float gain = 1.0f;
        if (now - started < 2000) {
            gain = (float) (now - started) / 2000.0f;
        }
        if (job->ends_ms - now < FADE_MS) {
            gain = (float) (job->ends_ms - now) / (float) FADE_MS;
        }
        if (gain < 1.0f) {
            for (int i = 0; i < NOISE_CHUNK; i++) {
                chunk[i] = (int16_t) ((float) chunk[i] * gain);
            }
        }
        xSemaphoreTake(gate, portMAX_DELAY);
        if (keep_going(mine)) {
            audio_play(chunk, NOISE_CHUNK);
        }
        xSemaphoreGive(gate);
    }
}

static void finish(uint32_t mine)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (generation == mine) {
        wanted.kind = KIND_NONE;
        wanted.title[0] = 0;
    }
    xSemaphoreGive(lock);
}

static void media_task(void *arg)
{
    (void) arg;
    static EXTERNAL_RAM wanted_t job;
    int failures = 0;
    uint32_t last = 0;
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        job = wanted;
        uint32_t mine = generation;
        xSemaphoreGive(lock);
        if (job.kind == KIND_NONE || held) {
            active = false;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            continue;
        }
        active = true;
        if (mine != last) {
            last = mine;
            failures = 0;
            stream_position = 0;
        }
        if (job.kind == KIND_NOISE) {
            run_noise(&job, mine);
            if (keep_going(mine)) {
                finish(mine);
            }
            continue;
        }
        failures++;
        bool complete = run_stream(&job, mine, &failures);
        if (!keep_going(mine)) {
            continue;
        }
        if (complete) {
            audio_finish();
            finish(mine);
        } else if (failures >= RETRIES) {
            ESP_LOGW(TAG, "%s keeps failing, giving up", job.title);
            finish(mine);
        } else {
            ESP_LOGW(TAG, "%s was interrupted, connecting again", job.title);
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
        }
    }
}

void media_start(void)
{
    lock = xSemaphoreCreateMutex();
    gate = xSemaphoreCreateMutex();
    hold_buffer = heap_caps_malloc(HOLD_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!lock || !gate || !hold_buffer) {
        ESP_LOGE(TAG, "not enough memory");
        return;
    }
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    xTaskCreatePinnedToCoreWithCaps(media_task, "media", 12288, NULL, 3, &worker, 1, MALLOC_CAP_SPIRAM);
}

void media_hold(bool hold)
{
    if (!gate) {
        return;
    }
    if (hold) {
        bool was_active = active && !held;
        xSemaphoreTake(gate, portMAX_DELAY);
        held = true;
        xSemaphoreGive(gate);
        if (was_active) {
            audio_flush();
        }
    } else if (held) {
        held = false;
        waiting = false;
        if (worker) {
            xTaskNotifyGive(worker);
        }
    }
}

bool media_waiting(void)
{
    return waiting;
}

bool media_audible(void)
{
    return active && !held;
}

size_t media_title(char *out, size_t capacity)
{
    out[0] = 0;
    if (!lock) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    if (wanted.kind != KIND_NONE) {
        strlcpy(out, wanted.title, capacity);
    }
    xSemaphoreGive(lock);
    return strlen(out);
}

static void request(kind_t kind, const char *title, const char *url, bool resumable, noise_kind_t noise, int minutes)
{
    xSemaphoreTake(gate, portMAX_DELAY);
    xSemaphoreTake(lock, portMAX_DELAY);
    generation++;
    wanted.kind = kind;
    wanted.resumable = resumable;
    wanted.noise = noise;
    wanted.ends_ms = clock_ms() + (int64_t) minutes * 60 * 1000;
    strlcpy(wanted.title, title, sizeof(wanted.title));
    strlcpy(wanted.url, url ? url : "", sizeof(wanted.url));
    xSemaphoreGive(lock);
    xSemaphoreGive(gate);
    if (held) {
        waiting = kind != KIND_NONE;
    } else {
        audio_flush();
    }
    if (worker) {
        xTaskNotifyGive(worker);
    }
}

void media_stop(void)
{
    if (lock) {
        request(KIND_NONE, "", NULL, false, NOISE_WHITE, 0);
    }
}

void media_play_station(int index)
{
    if (lock && index >= 0 && index < station_count()) {
        request(KIND_STREAM, station_name(index), station_url(index), false, NOISE_WHITE, 0);
    }
}

void media_play_noise(noise_kind_t kind, int minutes)
{
    static const char *const NAMES[] = {"Bílý šum", "Růžový šum", "Hnědý šum"};
    if (lock) {
        int length = minutes > 0 && minutes <= LONGEST_NOISE_MINUTES ? minutes : DEFAULT_NOISE_MINUTES;
        request(KIND_NOISE, NAMES[kind], NULL, false, kind, length);
    }
}

static char *reply(const char *key, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key, message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static char *playing_reply(const char *title)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", "spustí se, jakmile domluvíš");
    cJSON_AddStringToObject(root, "title", title);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static const char *argument(const cJSON *root, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(item) && item->valuestring[0] ? item->valuestring : NULL;
}

static void encode_query(const char *text, char *out, size_t capacity)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *c = (const unsigned char *) text; *c && used + 4 < capacity; c++) {
        bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.';
        if (plain) {
            out[used++] = (char) *c;
        } else {
            out[used++] = '%';
            out[used++] = HEX[*c >> 4];
            out[used++] = HEX[*c & 15];
        }
    }
    out[used] = 0;
}

static bool find_station_online(const char *name, char *title, size_t title_capacity, char *url, size_t url_capacity)
{
    char encoded[128];
    char address[256];
    encode_query(name, encoded, sizeof(encoded));
    snprintf(address, sizeof(address),
             "https://de1.api.radio-browser.info/json/stations/byname/%s?limit=5&order=votes&reverse=true&hidebroken=true", encoded);
    size_t length = 0;
    char *body = web_get(address, SEARCH_LIMIT, &length);
    cJSON *root = body ? cJSON_ParseWithLength(body, length) : NULL;
    free(body);
    bool found = false;
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        const char *codec = argument(item, "codec");
        const char *resolved = argument(item, "url_resolved");
        const cJSON *hls = cJSON_GetObjectItemCaseSensitive(item, "hls");
        if (codec && resolved && (!cJSON_IsNumber(hls) || hls->valueint == 0) && (strcasecmp(codec, "MP3") == 0 || strncasecmp(codec, "AAC", 3) == 0)) {
            const char *station = argument(item, "name");
            strlcpy(title, station ? station : name, title_capacity);
            strlcpy(url, resolved, url_capacity);
            found = true;
            break;
        }
    }
    cJSON_Delete(root);
    return found;
}

char *media_tool_radio(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const char *name = argument(args, "station");
    char *answer = NULL;
    if (!name) {
        answer = reply("error", "Chybí název stanice.");
    } else {
        int index = station_match(name);
        if (index >= 0) {
            media_play_station(index);
            answer = playing_reply(station_name(index));
        } else {
            char title[TITLE_SIZE];
            char *url = heap_caps_malloc(URL_SIZE, MALLOC_CAP_SPIRAM);
            if (url && find_station_online(name, title, sizeof(title), url, URL_SIZE)) {
                request(KIND_STREAM, title, url, false, NOISE_WHITE, 0);
                answer = playing_reply(title);
            } else {
                answer = reply("error", "Takovou stanici jsem nenašel.");
            }
            free(url);
        }
    }
    cJSON_Delete(args);
    return answer;
}

typedef struct {
    char *data;
    size_t length;
} rss_t;

static bool on_rss(void *context, const char *data, size_t length)
{
    rss_t *rss = context;
    size_t room = RSS_LIMIT - 1 - rss->length;
    size_t take = length < room ? length : room;
    memcpy(rss->data + rss->length, data, take);
    rss->length += take;
    rss->data[rss->length] = 0;
    return take == length && !strstr(rss->data, "</item>");
}

char *media_tool_podcast(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const char *name = argument(args, "name");
    if (!name) {
        cJSON_Delete(args);
        return reply("error", "Chybí název podcastu.");
    }
    char encoded[128];
    char *address = heap_caps_malloc(URL_SIZE, MALLOC_CAP_SPIRAM);
    char *answer = NULL;
    encode_query(name, encoded, sizeof(encoded));
    size_t length = 0;
    char *body = NULL;
    if (address) {
        snprintf(address, URL_SIZE, "https://itunes.apple.com/search?media=podcast&limit=3&country=CZ&term=%s", encoded);
        body = web_get(address, SEARCH_LIMIT, &length);
    }
    cJSON *found = body ? cJSON_ParseWithLength(body, length) : NULL;
    free(body);
    const cJSON *first = found ? cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(found, "results"), 0) : NULL;
    const char *feed = first ? argument(first, "feedUrl") : NULL;
    const char *show = first ? argument(first, "collectionName") : NULL;
    rss_t rss = {.data = feed ? heap_caps_malloc(RSS_LIMIT, MALLOC_CAP_SPIRAM) : NULL};
    if (rss.data) {
        rss.data[0] = 0;
        web_stream(feed, on_rss, &rss);
        char episode[160];
        if (rss_first_item(rss.data, rss.length, episode, sizeof(episode), address, URL_SIZE)) {
            char title[TITLE_SIZE];
            strlcpy(title, show ? show : name, sizeof(title));
            request(KIND_STREAM, title, address, true, NOISE_WHITE, 0);
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "result", "spustí se, jakmile domluvíš");
            cJSON_AddStringToObject(root, "podcast", title);
            cJSON_AddStringToObject(root, "episode", episode);
            answer = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            ESP_LOGI(TAG, "podcast %s: %s", title, episode);
        }
    }
    free(rss.data);
    free(address);
    cJSON_Delete(found);
    cJSON_Delete(args);
    return answer ? answer : reply("error", "Takový podcast jsem nenašel.");
}

char *media_tool_noise(const char *args_json)
{
    cJSON *args = cJSON_Parse(args_json);
    const char *kind = argument(args, "kind");
    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(args, "minutes");
    noise_kind_t chosen = kind && strcmp(kind, "white") == 0 ? NOISE_WHITE : kind && strcmp(kind, "pink") == 0 ? NOISE_PINK : NOISE_BROWN;
    media_play_noise(chosen, cJSON_IsNumber(minutes) ? minutes->valueint : DEFAULT_NOISE_MINUTES);
    cJSON_Delete(args);
    return playing_reply(chosen == NOISE_WHITE ? "bílý šum" : chosen == NOISE_PINK ? "růžový šum" : "hnědý šum");
}

char *media_tool_stop(void)
{
    media_stop();
    return reply("result", "zastaveno");
}

cJSON *media_json(void)
{
    cJSON *root = cJSON_CreateObject();
    char title[TITLE_SIZE];
    media_title(title, sizeof(title));
    cJSON_AddStringToObject(root, "title", title);
    cJSON_AddBoolToObject(root, "playing", title[0] != 0);
    cJSON *stations = cJSON_AddArrayToObject(root, "stations");
    for (int i = 0; i < station_count(); i++) {
        cJSON_AddItemToArray(stations, cJSON_CreateString(station_name(i)));
    }
    return root;
}
