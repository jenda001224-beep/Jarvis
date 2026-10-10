#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ikea.h"
#include "live.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "wake.h"

#define CONNECT_TIMEOUT_MS 12000
#define FIRST_TURN_TIMEOUT_MS 12000
#define FOLLOW_UP_TIMEOUT_MS 10000
#define STALLED_REPLY_TIMEOUT_MS 5000
#define SESSION_LIMIT_MS (9 * 60 * 1000)
#define ERROR_SHOWN_MS 5000
#define ECHO_GUARD_MS 300
#define UPLINK_SAMPLES 1024
#define REPORT_INTERVAL_MS 3000
#define VOICE_LEVEL 30
#define THINKING_AFTER_MS 700

typedef enum {
    STATE_OFFLINE,
    STATE_IDLE,
    STATE_CONNECTING,
    STATE_LISTENING,
    STATE_SPEAKING,
    STATE_ERROR,
} state_t;

typedef struct {
    char id[80];
    char name[40];
    char *args;
} tool_call_t;

static const char *TAG = "jarvis";

static const char PERSONA[] =
    "Jsi Jarvis, hlasový asistent na zdi v pokoji. Mluvíš česky, přirozeně, klidně a s lehkým suchým humorem. "
    "Odpovědi zazní nahlas, proto odpovídej jednou až dvěma krátkými větami a po splněném příkazu řekni jen pár "
    "slov. Uživatel tě oslovuje Jarvisi; oslovení může na začátku nahrávky chybět nebo být uříznuté, neřeš to. "
    "Anglická jména interpretů, skladeb, filmů a aplikací vyslovuj anglicky. Aktuální věci jako počasí nebo zprávy "
    "si vyhledej. Když se uživatel neptá na konkrétní místo, myslí %s. Když něco nevíš nebo se něco nepovedlo, "
    "řekni to krátce a na rovinu. Hudbu zatím pouštět neumíš; když o ni uživatel požádá, řekni to jednou větou. "
    "Tvůj displej umí posvítit: když uživatel řekne, ať mu dáš trochu světla, posvítíš nebo rozsvítíš displej, "
    "zavolej display_light s on true. Když řekne, ať displej nebo to světlo zhasneš, zavolej display_light "
    "s on false. Tenhle nástroj nepoužívej pro lampy, žárovky ani LED pásky.";

static const char HOME[] =
    "Ovládáš světla, LED pásky a zásuvky IKEA. Použij nástroj set_devices s id ze seznamu níže; stav zjistíš "
    "nástrojem get_devices. Barvu zadávej jako hue ve stupních (červená 0, oranžová 30, žlutá 55, zelená 120, "
    "tyrkysová 180, modrá 240, fialová 275, růžová 320) a saturation v procentech; bílé odstíny přes kelvin "
    "(teplá 2700, denní 4000, studená 6500). Když uživatel neřekne, které světlo myslí, vyber všechna světla "
    "v místnosti, o které mluví, a když neřekne ani místnost, vyber všechna světla. Nikdy neříkej, že jsi něco "
    "udělal, dokud nástroj nevrátí výsledek.";

static const char DISPLAY_FUNCTION[] =
    "{\"name\":\"display_light\",\"description\":\"Rozsvítí nebo zhasne displej asistenta jako bílou lampičku.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"on\":{\"type\":\"BOOLEAN\",\"description\":\"true rozsvítit, false zhasnout\"}},"
    "\"required\":[\"on\"]}}";

static const char HOME_FUNCTIONS[] =
    "{\"name\":\"set_devices\",\"description\":\"Zapne nebo vypne světla, LED pásky a zásuvky IKEA a nastaví jas "
    "a barvu světel.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"ids\":{\"type\":\"ARRAY\",\"items\":{\"type\":\"STRING\"},\"description\":\"ID zařízení ze seznamu\"},"
    "\"on\":{\"type\":\"BOOLEAN\",\"description\":\"true zapnout, false vypnout\"},"
    "\"brightness\":{\"type\":\"INTEGER\",\"description\":\"Jas 1 až 100\"},"
    "\"hue\":{\"type\":\"INTEGER\",\"description\":\"Odstín barvy 0 až 360 stupňů\"},"
    "\"saturation\":{\"type\":\"INTEGER\",\"description\":\"Sytost 0 až 100, výchozí 100\"},"
    "\"kelvin\":{\"type\":\"INTEGER\",\"description\":\"Teplota bílé 2000 až 6500\"}},"
    "\"required\":[\"ids\"]}},"
    "{\"name\":\"get_devices\",\"description\":\"Vrátí aktuální stav všech světel a zásuvek.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}}";

static QueueHandle_t events;
static QueueHandle_t tool_calls;
static state_t state = STATE_OFFLINE;
static volatile bool uplink_enabled;
static volatile bool new_turn;
static bool search_allowed = true;
static bool turn_complete;
static bool replied;
static int64_t state_since;
static int64_t session_since;
static int64_t last_activity;
static int64_t last_model_audio;
static int64_t last_voice;
static bool heard_any;
static bool thinking;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void app_post(app_event_t event)
{
    if (events) {
        xQueueSend(events, &event, 0);
    }
}

static void *allocate_external(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void on_heard(const char *text)
{
    if (new_turn) {
        new_turn = false;
        ui_clear_transcript();
    }
    ui_append_heard(text);
}

static void on_spoken(const char *text)
{
    ui_append_spoken(text);
}

static void on_tool(const char *id, const char *name, const char *args_json)
{
    tool_call_t call = {0};
    strlcpy(call.id, id, sizeof(call.id));
    strlcpy(call.name, name, sizeof(call.name));
    call.args = strdup(args_json);
    if (!call.args || xQueueSend(tool_calls, &call, 0) != pdTRUE) {
        free(call.args);
    }
}

static void tool_task(void *arg)
{
    tool_call_t call;
    for (;;) {
        if (xQueueReceive(tool_calls, &call, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "tool %s %s", call.name, call.args);
        char *result = NULL;
        if (strcmp(call.name, "display_light") == 0) {
            cJSON *args = cJSON_Parse(call.args);
            bool on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(args, "on"));
            cJSON_Delete(args);
            ui_set_lamp(on);
            live_send_tool_response(call.id, call.name, on ? "{\"result\":\"displej svítí\"}"
                                                           : "{\"result\":\"displej zhasnutý\"}");
            free(call.args);
            continue;
        }
        if (strcmp(call.name, "set_devices") == 0) {
            result = ikea_tool_set(call.args);
        } else if (strcmp(call.name, "get_devices") == 0) {
            result = ikea_tool_get();
        }
        live_send_tool_response(call.id, call.name, result ? result : "{\"error\":\"neznámý nástroj\"}");
        cJSON_free(result);
        free(call.args);
    }
}

static void uplink_task(void *arg)
{
    static int16_t pcm[UPLINK_SAMPLES];
    for (;;) {
        if (!uplink_enabled || !live_ready()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        size_t samples = wake_read_capture(pcm, UPLINK_SAMPLES, pdMS_TO_TICKS(40));
        if (samples > 0) {
            live_send_audio(pcm, samples);
        }
    }
}

static char *build_instruction(bool with_home)
{
    static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
    char moment[96] = "";
    if (net_time_valid()) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        snprintf(moment, sizeof(moment), "\n\nTeď je %s %d. %d. %d, %02d:%02d.", DAYS[local.tm_wday], local.tm_mday,
                 local.tm_mon + 1, local.tm_year + 1900, local.tm_hour, local.tm_min);
    }
    char *devices = with_home ? ikea_describe_devices() : NULL;
    size_t capacity = sizeof(PERSONA) + sizeof(HOME) + sizeof(moment) + 128 + (devices ? strlen(devices) : 0);
    char *text = allocate_external(capacity);
    if (text) {
        size_t used = (size_t) snprintf(text, capacity, PERSONA, settings_get()->place);
        used += (size_t) snprintf(text + used, capacity - used, "%s", moment);
        if (devices) {
            snprintf(text + used, capacity - used, "\n\n%s\n\n%s", HOME, devices);
        }
    }
    free(devices);
    return text;
}

static void enter(state_t next)
{
    state = next;
    state_since = now_ms();
}

static void show_hub(void)
{
    switch (ikea_state()) {
    case IKEA_UNPAIRED:
        ui_set_hub(UI_HUB_PAIR_OFFER, 0);
        break;
    case IKEA_PAIRING:
        ui_set_hub(UI_HUB_PAIRING, 0);
        break;
    case IKEA_READY:
        ui_set_hub(UI_HUB_READY, ikea_device_count());
        break;
    default:
        ui_set_hub(UI_HUB_HIDDEN, 0);
        break;
    }
}

static void go_idle(void)
{
    uplink_enabled = false;
    wake_set_capture(false);
    wake_clear_capture();
    audio_flush();
    live_close();
    wake_set_armed(true);
    if (net_connected()) {
        enter(STATE_IDLE);
        ui_set_mode(UI_IDLE, NULL);
    } else {
        enter(STATE_OFFLINE);
        ui_set_mode(UI_OFFLINE, NULL);
    }
}

static void fail(const char *message)
{
    uplink_enabled = false;
    wake_set_capture(false);
    wake_clear_capture();
    audio_flush();
    live_close();
    enter(STATE_ERROR);
    ui_set_mode(UI_ERROR, message);
}

static void start_session(bool by_voice)
{
    wake_set_armed(false);
    if (!by_voice) {
        wake_clear_capture();
        wake_set_capture(true);
    }
    bool with_home = ikea_state() == IKEA_READY && ikea_device_count() > 0;
    char *instruction = build_instruction(with_home);
    static char functions[sizeof(DISPLAY_FUNCTION) + sizeof(HOME_FUNCTIONS) + 8];
    snprintf(functions, sizeof(functions), "[%s%s%s]", DISPLAY_FUNCTION, with_home ? "," : "",
             with_home ? HOME_FUNCTIONS : "");
    esp_err_t err =
        instruction ? live_open(instruction, functions, search_allowed && settings_get()->search) : ESP_ERR_NO_MEM;
    free(instruction);
    if (err != ESP_OK) {
        fail("Nepodařilo se otevřít spojení");
        return;
    }
    turn_complete = false;
    replied = false;
    new_turn = true;
    heard_any = false;
    thinking = false;
    uplink_enabled = true;
    session_since = now_ms();
    last_voice = session_since;
    last_activity = session_since;
    ui_clear_transcript();
    enter(STATE_CONNECTING);
    ui_set_mode(UI_LISTENING, NULL);
}

static void resume_listening(void)
{
    vTaskDelay(pdMS_TO_TICKS(ECHO_GUARD_MS));
    wake_clear_capture();
    wake_set_capture(true);
    uplink_enabled = true;
    new_turn = true;
    turn_complete = false;
    replied = true;
    heard_any = false;
    thinking = false;
    last_activity = now_ms();
    last_voice = last_activity;
    enter(STATE_LISTENING);
    ui_set_mode(UI_LISTENING, NULL);
}

static void on_session_closed(void)
{
    if (state != STATE_CONNECTING && state != STATE_LISTENING && state != STATE_SPEAKING) {
        return;
    }
    if (live_was_ready()) {
        go_idle();
        return;
    }
    char message[160];
    const char *reason = live_close_reason();
    if (reason[0]) {
        snprintf(message, sizeof(message), "Gemini: %s", reason);
        if (settings_get()->search && search_allowed) {
            search_allowed = false;
        }
    } else {
        snprintf(message, sizeof(message), "Gemini se neozval. Zkontroluj Wi-Fi a klíč.");
    }
    fail(message);
}

static void handle(app_event_t event)
{
    bool in_session = state == STATE_CONNECTING || state == STATE_LISTENING || state == STATE_SPEAKING;
    switch (event) {
    case APP_EVENT_NETWORK:
        if (net_connected() && state == STATE_OFFLINE) {
            go_idle();
        } else if (!net_connected() && state != STATE_OFFLINE) {
            go_idle();
        }
        break;
    case APP_EVENT_WAKE:
        if (state == STATE_IDLE) {
            start_session(true);
        } else if (!in_session) {
            wake_set_capture(false);
            wake_clear_capture();
            wake_set_armed(true);
        }
        break;
    case APP_EVENT_TOUCH:
        if (ui_lamp()) {
            ui_set_lamp(false);
        } else if (state == STATE_IDLE) {
            start_session(false);
        } else if (in_session || state == STATE_ERROR) {
            go_idle();
        }
        break;
    case APP_EVENT_SESSION_READY:
        if (state == STATE_CONNECTING) {
            last_activity = now_ms();
            enter(STATE_LISTENING);
        }
        break;
    case APP_EVENT_MODEL_AUDIO:
        if (in_session) {
            uplink_enabled = false;
            wake_set_capture(false);
            live_send_stream_end();
            turn_complete = false;
            last_model_audio = now_ms();
            enter(STATE_SPEAKING);
            ui_set_mode(UI_SPEAKING, NULL);
        }
        break;
    case APP_EVENT_TURN_COMPLETE:
    case APP_EVENT_INTERRUPTED:
        if (state == STATE_SPEAKING) {
            turn_complete = true;
        } else if (state == STATE_LISTENING) {
            last_activity = now_ms();
        }
        break;
    case APP_EVENT_HEARD:
        last_activity = now_ms();
        heard_any = true;
        break;
    case APP_EVENT_SESSION_CLOSED:
        on_session_closed();
        break;
    case APP_EVENT_PAIR_REQUEST:
        ikea_request_pairing();
        break;
    case APP_EVENT_HUB_CHANGED:
        show_hub();
        break;
    case APP_EVENT_HUB_PAIRED:
        settings_write_hub_token(ikea_token());
        break;
    }
}

static void report(int64_t now)
{
    static int64_t last;
    static uint32_t last_frames;
    if (now - last < REPORT_INTERVAL_MS) {
        return;
    }
    uint32_t frames = wake_frame_count();
    int frames_per_second = 0;
    int frame_microseconds = 0;
    ui_render_stats(&frames_per_second, &frame_microseconds);
    ESP_LOGI(TAG, "state %d, mic peak %d in %u frames, display %d fps at %d us, heap %u internal (lowest %u), %u external",
             (int) state, wake_take_peak(), (unsigned) (frames - last_frames), frames_per_second, frame_microseconds,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    last = now;
    last_frames = frames;
}

static void supervise(void)
{
    int64_t now = now_ms();
    report(now);
    switch (state) {
    case STATE_CONNECTING:
        if (now - state_since > CONNECT_TIMEOUT_MS) {
            fail("Gemini se neozval. Zkontroluj Wi-Fi a klíč.");
        }
        break;
    case STATE_LISTENING:
        if (wake_microphone_level() > VOICE_LEVEL) {
            last_voice = now;
        }
        if (heard_any && thinking != (now - last_voice > THINKING_AFTER_MS)) {
            thinking = !thinking;
            ui_set_mode(thinking ? UI_THINKING : UI_LISTENING, NULL);
        }
        if (now - last_activity > (replied ? FOLLOW_UP_TIMEOUT_MS : FIRST_TURN_TIMEOUT_MS) ||
            now - session_since > SESSION_LIMIT_MS) {
            go_idle();
        }
        break;
    case STATE_SPEAKING:
        if (audio_playing()) {
            last_model_audio = now;
        } else if (turn_complete || now - last_model_audio > STALLED_REPLY_TIMEOUT_MS) {
            resume_listening();
        }
        break;
    case STATE_ERROR:
        if (now - state_since > ERROR_SHOWN_MS) {
            go_idle();
        }
        break;
    default:
        break;
    }
}

static void checkpoint(const char *stage)
{
    ESP_LOGI(TAG, "%s: %u internal free, largest block %u", stage,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void app_main(void)
{
    checkpoint("boot");
    cJSON_Hooks hooks = {.malloc_fn = allocate_external, .free_fn = free};
    cJSON_InitHooks(&hooks);
    events = xQueueCreate(32, sizeof(app_event_t));
    tool_calls = xQueueCreate(4, sizeof(tool_call_t));

    uint16_t *framebuffer = NULL;
    if (display_start(&framebuffer) != ESP_OK) {
        ESP_LOGE(TAG, "display failed to start");
    }
    bsp_display_backlight_on();
    checkpoint("display");
    ui_init(framebuffer);
    ui_set_mode(UI_STARTING, NULL);
    checkpoint("ui");

    bool configured = settings_load();
    if (audio_init(settings_get()->volume) != ESP_OK) {
        ESP_LOGE(TAG, "audio failed to start");
    } else {
        checkpoint("audio");
        if (wake_start() != ESP_OK) {
            ESP_LOGE(TAG, "wake word failed to start");
        }
    }
    checkpoint("wake word");
    if (!configured) {
        ui_set_mode(UI_SETUP_NEEDED, NULL);
        vTaskDelete(NULL);
        return;
    }

    const live_callbacks_t callbacks = {.heard = on_heard, .spoken = on_spoken, .tool = on_tool};
    live_init(&callbacks);
    xTaskCreatePinnedToCoreWithCaps(uplink_task, "uplink", 6144, NULL, 5, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(tool_task, "tools", 8192, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
    ui_set_mode(UI_OFFLINE, NULL);
    net_start(settings_get()->ssid, settings_get()->password);
    checkpoint("network");
    static char hub_token[1536];
    settings_read_hub_token(hub_token, sizeof(hub_token));
    ikea_start(hub_token);

    for (;;) {
        app_event_t event;
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle(event);
        }
        supervise();
    }
}
