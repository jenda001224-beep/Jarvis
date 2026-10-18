#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "api.h"
#include "app.h"
#include "audio.h"
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "calendar.h"
#include "console.h"
#include "control.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ikea.h"
#include "lists.h"
#include "live.h"
#include "memory.h"
#include "meteo.h"
#include "freertos/semphr.h"
#include "net.h"
#include "printer.h"
#include "reminders.h"
#include "search.h"
#include "settings.h"
#include "travel.h"
#include "ui.h"
#include "wake.h"
#include "weather.h"

#define CONNECT_TIMEOUT_MS 12000
#define CONNECT_ATTEMPTS 3
#define FIRST_TURN_TIMEOUT_MS 12000
#define FOLLOW_UP_TIMEOUT_MS 10000
#define ANSWER_TIMEOUT_MS 15000
#define STATUS_INTERVAL_MS 2000
#define RESERVE_MINUTES 15
#define CONTROL_PRIORITY 6
#define SEARCH_REFUSED "no_search"
#define SEARCH_REFUSED_DAY "no_search_day"
#define SECONDS_PER_DAY 86400
#define HOME_INTERVAL_MS 1000
#define STALLED_REPLY_TIMEOUT_MS 5000
#define SESSION_LIMIT_MS (9 * 60 * 1000)
#define ERROR_SHOWN_MS 5000
#define ECHO_GUARD_MS 300
#define ACK_QUIET_MS 300
#define ACK_WINDOW_MS 1200
#define NOISE_SMOOTHING 0.03f
#define SPEECH_ABOVE_NOISE 15
#define LOOP_WAIT_MS 100
#define NVS_APP_TOKENS "app_tokens"
#define NVS_REMINDERS "reminders"
#define PAIRING_MS 120000
#define PAIRING_ATTEMPTS 5
#define ALARM_GAP_MS 2500
#define ALARM_RING_LIMIT_MS (10 * 60 * 1000)
#define ALARM_MIN_VOLUME 60
#define RESTART_DELAY_MS 1500
#define SAY_LIMIT 160
#define ACK_LOOP_WAIT_MS 20
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

typedef enum {
    SESSION_VOICE,
    SESSION_TOUCH,
    SESSION_ANNOUNCE,
    SESSION_TEXT,
} session_kind_t;

typedef struct {
    char id[80];
    char name[40];
    char *args;
} tool_call_t;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} text_t;

static const char *TAG = "jarvis";

static const char PERSONA[] =
    "Jsi Jarvis, osobní asistent po vzoru Jarvise z Iron Mana. Visíš na zdi v pokoji svého pána. Mluvíš česky, "
    "pánovi vykáš a oslovuješ ho „pane“. Pán mluví česky, občas anglicky; jiné jazyky nepřipadají v úvahu. Když něco "
    "zní jako jiný jazyk, je to nezřetelná čeština, tak tomu tak rozuměj. Anglicky odpovídej, jen když pán celou "
    "větu řekne anglicky. Jsi zdvořilý, klidný a pohotový, se suchým britským humorem: občas si "
    "dovolíš krátkou ironickou poznámku, ale nikdy nejsi drzý ani upovídaný. Když pán řekne třeba „pojď si trochu "
    "povídat“, odpovíš ve stylu „To je trochu random, pane, ale dobře. O čem si chcete povídat?“ "
    "Odpovědi zazní nahlas, proto mluv jednou až dvěma krátkými větami; po splněném příkazu stačí pár slov, třeba "
    "„Hotovo, pane.“ Pán tě oslovuje Jarvisi; oslovení může na začátku nahrávky chybět nebo být uříznuté, neřeš to. "
    "Anglická jména interpretů, skladeb, filmů a aplikací vyslovuj anglicky. Počasí a předpověď zjišťuj nástrojem "
    "get_weather. Když se pán neptá na konkrétní místo, myslí %s. Když něco nevíš nebo se něco nepovedlo, řekni to "
    "krátce a na rovinu. Nikdy netvrď, že jsi něco udělal nebo zjistil, dokud ti to nástroj nepotvrdí. Hudbu zatím "
    "pouštět neumíš; když o ni pán požádá, řekni to jednou větou. "
    "Tvůj displej umí posvítit: když pán řekne, ať mu dáš trochu světla, posvítíš nebo rozsvítíš displej, zavolej "
    "display_light s on true. Když řekne, ať displej nebo to světlo zhasneš, zavolej display_light s on false. "
    "Tenhle nástroj nepoužívej pro lampy, žárovky ani LED pásky. "
    "Když pán chce něco připomenout nebo dát vědět v určitý čas, zavolej set_reminder a do text napiš celou větou, "
    "co mu máš říct. Když ti systém oznámí, že nastal čas připomínky, promluv na pána sám: oslov ho, řekni mu to "
    "a zeptej se, jestli tě slyší. Vedeš pánovi seznam úkolů: zapisuješ add_todo, čteš list_todos, odškrtáváš "
    "complete_todo a mažeš remove_todo; seznam vidí i v telefonu. Budík (set_alarm) zvoní každý den nebo ve "
    "vybrané dny, připomínka jen jednou promluví; když pán řekne „vzbuď mě“ nebo „budík“, nastav budík.";

static const char ALARM_GREETING[] =
    "Právě zazvonil budík%s a pán ho vypnul. Pozdrav ho podle denní doby, řekni kolik je hodin a ve třech krátkých "
    "větách, co ho dnes čeká: počasí, události z kalendáře a nesplněné úkoly, pokud na ně máš nástroje.";

static const char PHONE_MESSAGE[] =
    "Pán ti z telefonu poslal vzkaz pro lidi v pokoji. Přečti ho nahlas přesně tak, jak je, nic nepřidávej: „%s“";

static const char SEARCHING[] = "Zprávy a jiné čerstvé informace si vyhledej na internetu.";

static const char OWN_SEARCHING[] =
    "Aktuální informace z internetu, třeba otevírací doby, zprávy, ceny, kontakty nebo program akcí, zjistíš "
    "nástrojem search_web. Než ho zavoláš, řekni pánovi krátce, že se podíváš. Odpověz stručně podle výsledků; "
    "když v nich odpověď není, řekni to a nic si nedomýšlej.";

static const char NOT_SEARCHING[] =
    "Na internetu teď hledat neumíš. Když se pán ptá na zprávy nebo jiné čerstvé informace, řekni to jednou větou.";

static const char HOME[] =
    "Ovládáš světla, LED pásky a zásuvky IKEA. Použij nástroj set_devices s id ze seznamu níže; stav zjistíš "
    "nástrojem get_devices. Barvu zadávej jako hue ve stupních (červená 0, oranžová 30, žlutá 55, zelená 120, "
    "tyrkysová 180, modrá 240, fialová 275, růžová 320) a saturation v procentech; bílé odstíny přes kelvin "
    "(teplá 2700, denní 4000, studená 6500). Když pán neřekne, které světlo myslí, vyber všechna světla "
    "v místnosti, o které mluví, a když neřekne ani místnost, vyber všechna světla.";

static const char PRINTER[] =
    "Pán má 3D tiskárnu Bambu Lab A1. Na otázky o tisku nebo tiskárně zavolej printer_status a řekni jen to, na co "
    "se ptá: jak je tisk daleko a kolik zbývá, nebo proč tiskárna stojí. Popisy problémů přijdou anglicky; řekni "
    "je česky, vlastními slovy a krátce, například „Tiskárna má ucpanou trysku, pane.“";

static const char CALENDAR[] =
    "Kalendář pána čteš nástrojem get_calendar; bez parametrů vrátí dnešek, jinak date ve tvaru RRRR-MM-DD a days. "
    "Když se pán ptá, co má v plánu, řekni stručně časy a názvy.";

static const char TRAVEL[] =
    "Když má událost uvedené místo, zjisti nástrojem travel_time, jak dlouho tam cesta z domova trvá, a řekni to "
    "i s dopravním prostředkem. Pak nabídni, že dáš vědět, až bude čas vyrazit. Když pán souhlasí, nastav "
    "set_reminder na začátek události minus doba cesty minus %d minut rezervy.";

static const char ANNOUNCEMENT[] =
    "Zpráva od systému: nastal čas připomínky „%s“. Oslov pána, řekni mu to jednou nebo dvěma větami a zeptej se, "
    "jestli tě slyší.";

static const char DISPLAY_FUNCTION[] =
    "{\"name\":\"display_light\",\"description\":\"Rozsvítí nebo zhasne displej asistenta jako bílou lampičku.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"on\":{\"type\":\"BOOLEAN\",\"description\":\"true rozsvítit, false zhasnout\"}},"
    "\"required\":[\"on\"]}}";

static const char REMINDER_FUNCTIONS[] =
    "{\"name\":\"set_reminder\",\"description\":\"Nastaví připomínku. V daný čas Jarvis sám promluví a řekne text.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"text\":{\"type\":\"STRING\",\"description\":\"Co má Jarvis říct, celou větou\"},"
    "\"time\":{\"type\":\"STRING\",\"description\":\"Čas ve tvaru HH:MM\"},"
    "\"date\":{\"type\":\"STRING\",\"description\":\"Datum RRRR-MM-DD; bez něj nejbližší takový čas\"},"
    "\"in_minutes\":{\"type\":\"INTEGER\",\"description\":\"Místo času: za kolik minut od teď\"}},"
    "\"required\":[\"text\"]}},"
    "{\"name\":\"list_reminders\",\"description\":\"Vrátí nastavené připomínky.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"cancel_reminder\",\"description\":\"Zruší připomínku podle id z list_reminders.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"id\":{\"type\":\"INTEGER\"}},\"required\":[\"id\"]}}";

static const char LIST_FUNCTIONS[] =
    "{\"name\":\"add_todo\",\"description\":\"Zapíše úkol na pánův seznam úkolů.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"text\":{\"type\":\"STRING\"}},\"required\":[\"text\"]}},"
    "{\"name\":\"list_todos\",\"description\":\"Vrátí seznam úkolů i se splněnými.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"complete_todo\",\"description\":\"Odškrtne úkol jako splněný podle id nebo přesného textu.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"id\":{\"type\":\"INTEGER\"},\"text\":{\"type\":\"STRING\"}}}},"
    "{\"name\":\"remove_todo\",\"description\":\"Smaže úkol ze seznamu podle id nebo přesného textu.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"id\":{\"type\":\"INTEGER\"},\"text\":{\"type\":\"STRING\"}}}},"
    "{\"name\":\"set_alarm\",\"description\":\"Nastaví budík, který v daný čas zazvoní.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"time\":{\"type\":\"STRING\",\"description\":\"Čas ve tvaru HH:MM\"},"
    "\"days\":{\"type\":\"ARRAY\",\"items\":{\"type\":\"INTEGER\"},"
    "\"description\":\"Dny opakování 1 pondělí až 7 neděle; prázdné znamená jen jednou\"},"
    "\"label\":{\"type\":\"STRING\",\"description\":\"Krátký popis, třeba Do práce\"}},\"required\":[\"time\"]}},"
    "{\"name\":\"list_alarms\",\"description\":\"Vrátí nastavené budíky.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
    "{\"name\":\"cancel_alarm\",\"description\":\"Smaže budík podle id z list_alarms.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"id\":{\"type\":\"INTEGER\"}},\"required\":[\"id\"]}}";

static const char SEARCH_FUNCTION[] =
    "{\"name\":\"search_web\",\"description\":\"Vyhledá na internetu aktuální informace a vrátí krátkou odpověď "
    "a nalezené stránky.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"query\":{\"type\":\"STRING\","
    "\"description\":\"Dotaz jako do vyhledávače, s místem a časem, když na nich záleží\"}},\"required\":[\"query\"]}}";

static const char WEATHER_FUNCTION[] =
    "{\"name\":\"get_weather\",\"description\":\"Vrátí aktuální počasí a předpověď po dnech.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"place\":{\"type\":\"STRING\",\"description\":\"Město nebo obec; bez něj domovské místo pána\"},"
    "\"days\":{\"type\":\"INTEGER\",\"description\":\"Počet dní včetně dneška, 1 až 7, výchozí 2\"}}}}";

static const char PRINTER_FUNCTION[] =
    "{\"name\":\"printer_status\",\"description\":\"Vrátí stav 3D tiskárny: co dělá, kolik procent a minut zbývá "
    "a případné problémy.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}}";

static const char CALENDAR_FUNCTION[] =
    "{\"name\":\"get_calendar\",\"description\":\"Vrátí události z kalendáře pána.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"date\":{\"type\":\"STRING\",\"description\":\"První den RRRR-MM-DD, výchozí dnešek\"},"
    "\"days\":{\"type\":\"INTEGER\",\"description\":\"Počet dní, 1 až 14\"}}}}";

static const char TRAVEL_FUNCTION[] =
    "{\"name\":\"travel_time\",\"description\":\"Spočítá, jak dlouho trvá cesta z domova na dané místo.\","
    "\"parameters\":{\"type\":\"OBJECT\",\"properties\":{"
    "\"destination\":{\"type\":\"STRING\",\"description\":\"Adresa nebo název místa, nejlépe i s městem\"},"
    "\"mode\":{\"type\":\"STRING\",\"enum\":[\"scooter\",\"bike\",\"walk\",\"car\"],"
    "\"description\":\"Dopravní prostředek; bez něj ten, kterým pán jezdí obvykle\"}},"
    "\"required\":[\"destination\"]}}";

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
static SemaphoreHandle_t reminder_lock;
static state_t state = STATE_OFFLINE;
static volatile bool uplink_enabled;
static volatile bool uplinked;
static volatile bool new_turn;
static volatile bool tool_busy;
static volatile int heard_letters;
static bool search_allowed = true;
static uint32_t key_mark;
static uint32_t reminders_revision;

static void next_alarm_text(char *out, size_t capacity);

extern const uint8_t ack_ano_start[] asm("_binary_ack_ano_pcm_start");
extern const uint8_t ack_ano_end[] asm("_binary_ack_ano_pcm_end");
extern const uint8_t ack_posloucham_start[] asm("_binary_ack_posloucham_pcm_start");
extern const uint8_t ack_posloucham_end[] asm("_binary_ack_posloucham_pcm_end");
extern const uint8_t ack_prosim_start[] asm("_binary_ack_prosim_pcm_start");
extern const uint8_t ack_prosim_end[] asm("_binary_ack_prosim_pcm_end");

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} clip_t;

static const clip_t ACKNOWLEDGEMENTS[] = {
    {ack_ano_start, ack_ano_end},
    {ack_posloucham_start, ack_posloucham_end},
    {ack_prosim_start, ack_prosim_end},
};

static float noise_floor;
static bool ack_pending;
static bool acking;
static int64_t ack_since;
static int64_t ack_loud;
static int64_t ack_ended;
static bool turn_complete;
static bool replied;
static int64_t state_since;
static int64_t session_since;
static int64_t last_activity;
static int64_t last_model_audio;
static int64_t last_voice;
static bool heard_any;
static bool thinking;
static session_kind_t session_kind;
static uint32_t announcing;
static bool announced;
static bool awaiting_answer;
static bool delivering_unheard;
static bool dry;
static char opening[sizeof(ANNOUNCEMENT) + CONSOLE_LINE];

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

static void append(text_t *text, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int needed = vsnprintf(NULL, 0, format, arguments);
    va_end(arguments);
    if (needed < 0) {
        return;
    }
    if (text->length + (size_t) needed + 1 > text->capacity) {
        size_t capacity = text->capacity ? text->capacity : 2048;
        while (capacity < text->length + (size_t) needed + 1) {
            capacity *= 2;
        }
        char *grown = heap_caps_realloc(text->data, capacity, MALLOC_CAP_SPIRAM);
        if (!grown) {
            return;
        }
        text->data = grown;
        text->capacity = capacity;
    }
    va_start(arguments, format);
    vsnprintf(text->data + text->length, text->capacity - text->length, format, arguments);
    va_end(arguments);
    text->length += (size_t) needed;
}

static char *reply(const char *key, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, key, message);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static void format_moment(int64_t moment, char *out, size_t capacity)
{
    time_t value = (time_t) moment;
    struct tm local;
    localtime_r(&value, &local);
    snprintf(out, capacity, "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min);
}

static void on_heard(const char *text)
{
    if (new_turn) {
        new_turn = false;
        ui_clear_transcript();
    }
    int letters = 0;
    for (const char *c = text; *c; c++) {
        unsigned char value = (unsigned char) *c;
        letters += (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
                   (value >= 'a' && value <= 'z') || value >= 0xC0;
    }
    heard_letters += letters;
    ESP_LOGI(TAG, "heard: %s", text);
    ui_append_heard(text);
}

static void on_spoken(const char *text)
{
    ESP_LOGI(TAG, "said: %s", text);
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

static char *tool_display(const cJSON *args)
{
    bool on = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(args, "on"));
    ui_set_lamp(on);
    return reply("result", on ? "displej svítí" : "displej zhasnutý");
}

static char *tool_set_reminder(const cJSON *args)
{
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(args, "text");
    const cJSON *clock = cJSON_GetObjectItemCaseSensitive(args, "time");
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(args, "date");
    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(args, "in_minutes");
    if (!net_time_valid()) {
        return reply("error", "Ještě neznám přesný čas.");
    }
    if (!cJSON_IsString(text) || !text->valuestring[0]) {
        return reply("error", "Chybí text připomínky.");
    }
    time_t now = time(NULL);
    time_t due = 0;
    int hour;
    int minute;
    if (cJSON_IsNumber(minutes) && minutes->valuedouble >= 1 && minutes->valuedouble <= 525600) {
        due = now + (time_t) minutes->valuedouble * 60;
    } else if (cJSON_IsString(clock) && sscanf(clock->valuestring, "%d:%d", &hour, &minute) == 2 && hour >= 0 &&
               hour <= 23 && minute >= 0 && minute <= 59) {
        struct tm local;
        localtime_r(&now, &local);
        int year;
        int month;
        int day;
        bool dated = cJSON_IsString(date) && sscanf(date->valuestring, "%d-%d-%d", &year, &month, &day) == 3 &&
                     year >= 2024 && year < 2200 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
        if (dated) {
            local.tm_year = year - 1900;
            local.tm_mon = month - 1;
            local.tm_mday = day;
        }
        local.tm_hour = hour;
        local.tm_min = minute;
        local.tm_sec = 0;
        local.tm_isdst = -1;
        due = mktime(&local);
        if (!dated && due <= now) {
            local.tm_mday++;
            local.tm_isdst = -1;
            due = mktime(&local);
        }
    } else {
        return reply("error", "Chybí čas připomínky.");
    }
    if (due <= now) {
        return reply("error", "Ten čas už minul.");
    }
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    uint32_t id = reminders_add(due, text->valuestring);
    xSemaphoreGive(reminder_lock);
    if (!id) {
        return reply("error", "Víc připomínek si nezapamatuji, nejdřív nějakou zruš.");
    }
    app_post(APP_EVENT_REMINDERS_CHANGED);
    char moment[64];
    format_moment(due, moment, sizeof(moment));
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "result", "připomínka nastavena");
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddStringToObject(root, "at", moment);
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return printed;
}

static char *tool_list_reminders(void)
{
    reminder_t pending[REMINDERS_MAX];
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    int count = reminders_pending(pending, REMINDERS_MAX);
    xSemaphoreGive(reminder_lock);
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "reminders");
    for (int i = 0; i < count; i++) {
        char moment[64];
        format_moment(pending[i].due, moment, sizeof(moment));
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", pending[i].id);
        cJSON_AddStringToObject(item, "at", moment);
        cJSON_AddStringToObject(item, "text", pending[i].text);
        cJSON_AddItemToArray(list, item);
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return printed;
}

static char *tool_cancel_reminder(const cJSON *args)
{
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(args, "id");
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    bool cancelled = cJSON_IsNumber(id) && id->valuedouble >= 1 && reminders_cancel((uint32_t) id->valuedouble);
    xSemaphoreGive(reminder_lock);
    if (!cancelled) {
        return reply("error", "Takovou připomínku nemám.");
    }
    app_post(APP_EVENT_REMINDERS_CHANGED);
    return reply("result", "připomínka zrušena");
}

static char *run_tool(const tool_call_t *call)
{
    cJSON *args = cJSON_Parse(call->args);
    char *result = NULL;
    if (strcmp(call->name, "display_light") == 0) {
        result = tool_display(args);
    } else if (strcmp(call->name, "set_devices") == 0) {
        result = ikea_tool_set(call->args);
    } else if (strcmp(call->name, "get_devices") == 0) {
        result = ikea_tool_get();
    } else if (strcmp(call->name, "get_weather") == 0) {
        result = weather_tool(call->args);
    } else if (strcmp(call->name, "search_web") == 0) {
        result = search_tool(call->args);
    } else if (strcmp(call->name, "add_todo") == 0) {
        result = lists_tool_add_todo(call->args);
    } else if (strcmp(call->name, "list_todos") == 0) {
        result = lists_tool_list_todos();
    } else if (strcmp(call->name, "complete_todo") == 0) {
        result = lists_tool_complete_todo(call->args);
    } else if (strcmp(call->name, "remove_todo") == 0) {
        result = lists_tool_remove_todo(call->args);
    } else if (strcmp(call->name, "set_alarm") == 0) {
        result = lists_tool_set_alarm(call->args);
    } else if (strcmp(call->name, "list_alarms") == 0) {
        result = lists_tool_list_alarms();
    } else if (strcmp(call->name, "cancel_alarm") == 0) {
        result = lists_tool_cancel_alarm(call->args);
    } else if (strcmp(call->name, "printer_status") == 0) {
        result = printer_tool_status();
    } else if (strcmp(call->name, "get_calendar") == 0) {
        result = calendar_tool_get(call->args);
    } else if (strcmp(call->name, "travel_time") == 0) {
        result = travel_tool(call->args);
    } else if (strcmp(call->name, "set_reminder") == 0) {
        result = tool_set_reminder(args);
    } else if (strcmp(call->name, "list_reminders") == 0) {
        result = tool_list_reminders();
    } else if (strcmp(call->name, "cancel_reminder") == 0) {
        result = tool_cancel_reminder(args);
    }
    cJSON_Delete(args);
    return result;
}

static void tool_task(void *arg)
{
    tool_call_t call;
    for (;;) {
        if (xQueueReceive(tool_calls, &call, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG, "tool %s %s", call.name, call.args);
        tool_busy = true;
        char *result = run_tool(&call);
        ESP_LOGI(TAG, "tool answer %.240s", result ? result : "none");
        live_send_tool_response(call.id, call.name, result ? result : "{\"error\":\"neznámý nástroj\"}");
        tool_busy = false;
        cJSON_free(result);
        free(call.args);
    }
}

static void uplink_task(void *arg)
{
    static EXTERNAL_RAM int16_t pcm[UPLINK_SAMPLES];
    for (;;) {
        if (!uplink_enabled || !live_ready()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        size_t samples = wake_read_capture(pcm, UPLINK_SAMPLES, pdMS_TO_TICKS(40));
        if (samples > 0 && live_send_audio(pcm, samples)) {
            uplinked = true;
        }
    }
}

static bool native_search(void)
{
    return search_allowed && settings_get()->search;
}

static char *build_instruction(bool with_home, const reminder_t *unheard, int unheard_count)
{
    static const char *const DAYS[] = {"neděle", "pondělí", "úterý", "středa", "čtvrtek", "pátek", "sobota"};
    text_t text = {0};
    append(&text, PERSONA, settings_get()->place);
    append(&text, " %s", native_search() ? SEARCHING : search_configured() ? OWN_SEARCHING : NOT_SEARCHING);
    if (net_time_valid()) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        append(&text, "\n\nTeď je %s %d. %d. %d, %02d:%02d.", DAYS[local.tm_wday], local.tm_mday, local.tm_mon + 1,
               local.tm_year + 1900, local.tm_hour, local.tm_min);
    }
    if (unheard_count > 0) {
        append(&text, "\n\nPán neslyšel tyto připomínky. Řekni mu je hned na začátku první odpovědi:");
        for (int i = 0; i < unheard_count; i++) {
            char moment[64];
            format_moment(unheard[i].due, moment, sizeof(moment));
            append(&text, " „%s“ (mělo zaznít %s).", unheard[i].text, moment + 11);
        }
    }
    if (printer_configured()) {
        append(&text, "\n\n%s", PRINTER);
    }
    if (calendar_configured()) {
        char *today = allocate_external(2048);
        append(&text, "\n\n%s", CALENDAR);
        if (travel_configured()) {
            append(&text, " ");
            append(&text, TRAVEL, RESERVE_MINUTES);
        }
        if (today && calendar_today(today, 2048) > 0) {
            append(&text, " Dnešní program pána: %s.", today);
        }
        free(today);
    }
    if (with_home) {
        char *devices = ikea_describe_devices();
        append(&text, "\n\n%s\n\n%s", HOME, devices ? devices : "");
        free(devices);
    }
    return text.data;
}

static char *build_functions(bool with_home)
{
    text_t text = {0};
    append(&text, "[%s,%s,%s,%s", DISPLAY_FUNCTION, REMINDER_FUNCTIONS, LIST_FUNCTIONS, WEATHER_FUNCTION);
    if (!native_search() && search_configured()) {
        append(&text, ",%s", SEARCH_FUNCTION);
    }
    if (printer_configured()) {
        append(&text, ",%s", PRINTER_FUNCTION);
    }
    if (calendar_configured()) {
        append(&text, ",%s", CALENDAR_FUNCTION);
        if (travel_configured()) {
            append(&text, ",%s", TRAVEL_FUNCTION);
        }
    }
    if (with_home) {
        append(&text, ",%s", HOME_FUNCTIONS);
    }
    append(&text, "]");
    return text.data;
}

static void show_reminders(void)
{
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    int unheard = reminders_unheard(NULL, 0);
    xSemaphoreGive(reminder_lock);
    ui_set_notice(unheard > 0 ? "PŘIPOMÍNKA ČEKÁ · DOTKNI SE" : NULL);
}

static void save_reminders(void)
{
    static uint8_t blob[sizeof(reminder_t) * REMINDERS_MAX + 16];
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    size_t length = reminders_save(blob, sizeof(blob));
    xSemaphoreGive(reminder_lock);
    if (length) {
        settings_write_blob(NVS_REMINDERS, blob, length);
    }
    reminders_revision++;
    show_reminders();
}

static void end_announcement(bool answered)
{
    if (announcing && answered) {
        xSemaphoreTake(reminder_lock, portMAX_DELAY);
        reminders_cancel(announcing);
        xSemaphoreGive(reminder_lock);
    }
    bool changed = announcing != 0;
    announcing = 0;
    announced = false;
    awaiting_answer = false;
    delivering_unheard = false;
    if (changed) {
        save_reminders();
    }
}

static void enter(state_t next)
{
    state = next;
    state_since = now_ms();
}

static void go_idle(void)
{
    ack_pending = false;
    acking = false;
    end_announcement(false);
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
    ack_pending = false;
    acking = false;
    end_announcement(false);
    uplink_enabled = false;
    wake_set_capture(false);
    wake_clear_capture();
    audio_flush();
    live_close();
    enter(STATE_ERROR);
    ui_set_mode(UI_ERROR, message);
}

static void retry_refused_search(void)
{
    if (search_allowed || !settings_get()->search || !net_time_valid()) {
        return;
    }
    uint32_t refused_day = settings_read_number(SEARCH_REFUSED_DAY);
    if ((uint32_t) (time(NULL) / SECONDS_PER_DAY) > refused_day) {
        ESP_LOGI(TAG, "trying search again, refused on day %u", (unsigned) refused_day);
        search_allowed = true;
    }
}

static esp_err_t open_connection(void)
{
    reminder_t unheard[REMINDERS_MAX];
    int unheard_count = 0;
    if (session_kind != SESSION_ANNOUNCE) {
        xSemaphoreTake(reminder_lock, portMAX_DELAY);
        unheard_count = reminders_unheard(unheard, REMINDERS_MAX);
        xSemaphoreGive(reminder_lock);
    }
    retry_refused_search();
    bool with_home = ikea_state() == IKEA_READY && ikea_device_count() > 0;
    char *instruction = build_instruction(with_home, unheard, unheard_count);
    char *functions = build_functions(with_home);
    esp_err_t err = instruction && functions
                        ? live_open(instruction, functions, native_search())
                        : ESP_ERR_NO_MEM;
    free(instruction);
    free(functions);
    delivering_unheard = err == ESP_OK && unheard_count > 0;
    return err;
}

static int connect_attempt;

static bool connect_again(const char *why)
{
    while (connect_attempt < CONNECT_ATTEMPTS) {
        connect_attempt++;
        ESP_LOGW(TAG, "%s, connecting again (attempt %d of %d)", why, connect_attempt, CONNECT_ATTEMPTS);
        if (open_connection() == ESP_OK) {
            return true;
        }
    }
    return false;
}

static void start_session(session_kind_t kind)
{
    wake_set_armed(false);
    bool spoken_first = kind == SESSION_ANNOUNCE || kind == SESSION_TEXT;
    if (kind == SESSION_TOUCH) {
        wake_clear_capture();
        wake_set_capture(true);
    } else if (spoken_first) {
        wake_set_capture(false);
        wake_clear_capture();
    }
    session_kind = kind;
    connect_attempt = 1;
    esp_err_t opened = open_connection();
    if (opened != ESP_OK) {
        ESP_LOGE(TAG, "opening the session failed: %s, %u internal free, largest block %u", esp_err_to_name(opened),
                 (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        if (!connect_again("opening failed")) {
            fail("Nepodařilo se otevřít spojení");
            return;
        }
    }
    announced = false;
    awaiting_answer = false;
    turn_complete = false;
    replied = false;
    new_turn = true;
    heard_any = false;
    thinking = false;
    uplinked = false;
    heard_letters = 0;
    uplink_enabled = !spoken_first;
    session_since = now_ms();
    last_voice = session_since;
    last_activity = session_since;
    ack_pending = kind == SESSION_VOICE;
    acking = false;
    ack_since = session_since;
    ack_loud = session_since;
    ui_clear_transcript();
    enter(STATE_CONNECTING);
    ui_set_mode(spoken_first ? UI_THINKING : UI_LISTENING, NULL);
}

static void announce_due(void)
{
    reminder_t due;
    if (!net_time_valid()) {
        return;
    }
    time_t now = time(NULL);
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    bool found = reminders_due(now, &due);
    if (found) {
        reminders_defer(due.id, now + REMINDER_RETRY_SECONDS);
    }
    xSemaphoreGive(reminder_lock);
    if (!found) {
        return;
    }
    ESP_LOGI(TAG, "reminder %u is due", (unsigned) due.id);
    snprintf(opening, sizeof(opening), ANNOUNCEMENT, due.text);
    announcing = due.id;
    start_session(SESSION_ANNOUNCE);
}

static void resume_listening(void)
{
    vTaskDelay(pdMS_TO_TICKS(ECHO_GUARD_MS));
    wake_clear_capture();
    wake_set_capture(true);
    heard_letters = 0;
    awaiting_answer = announcing != 0;
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
            settings_write_number(SEARCH_REFUSED, key_mark);
            if (net_time_valid()) {
                settings_write_number(SEARCH_REFUSED_DAY, (uint32_t) (time(NULL) / SECONDS_PER_DAY));
            }
            ESP_LOGW(TAG, "trying again without search");
            if (open_connection() == ESP_OK) {
                enter(STATE_CONNECTING);
                return;
            }
        }
    } else {
        if (connect_again("connection failed")) {
            enter(STATE_CONNECTING);
            return;
        }
        snprintf(message, sizeof(message), "Gemini se neozval. Zkontroluj Wi-Fi a klíč.");
    }
    fail(message);
}

static home_prefs_t prefs;

static uint32_t paired_value(int base, int value)
{
    return ((uint32_t) (base + 1) << 8) | (uint32_t) (value + 1);
}

static int stored_value(const char *name, int fallback, int low, int high)
{
    uint32_t stored = settings_read_number(name);
    int value = stored ? (int) (stored & 0xFF) - 1 : fallback;
    return value < low || value > high ? fallback : value;
}

static int stored_pair(const char *name, int base, int low, int high)
{
    uint32_t stored = settings_read_number(name);
    if (!stored || (int) (stored >> 8) != base + 1) {
        return base;
    }
    int value = (int) (stored & 0xFF) - 1;
    return value < low || value > high ? base : value;
}

static int stored_minutes(const char *name, int fallback)
{
    uint32_t stored = settings_read_number(name);
    int value = stored ? (int) stored - 1 : fallback;
    return value < 0 || value >= 24 * 60 ? fallback : value;
}

static void load_prefs(void)
{
    const settings_t *settings = settings_get();
    prefs.volume = stored_pair("volume", settings->volume, 0, 100);
    prefs.sensitivity = stored_pair("sensitivity", settings->sensitivity, 0, 2);
    prefs.brightness = stored_value("brightness", 100, 10, 100);
    prefs.sleep = stored_value("sleep", 1, 0, HOME_SLEEP_CHOICES - 1);
    prefs.dark = stored_value("dark", 0, 0, 1);
    prefs.orientation = stored_value("orientation", 0, 0, 3);
    prefs.night = stored_value("night", 1, 0, 1);
    prefs.night_from = stored_minutes("night_from", 23 * 60);
    prefs.night_to = stored_minutes("night_to", 7 * 60);
}

static void apply_prefs(home_prefs_t chosen)
{
    const settings_t *settings = settings_get();
    if (chosen.volume != prefs.volume) {
        audio_set_volume(chosen.volume);
        settings_write_number("volume", paired_value(settings->volume, chosen.volume));
    }
    if (chosen.sensitivity != prefs.sensitivity) {
        wake_set_sensitivity(chosen.sensitivity);
        settings_write_number("sensitivity", paired_value(settings->sensitivity, chosen.sensitivity));
    }
    if (chosen.brightness != prefs.brightness) {
        settings_write_number("brightness", (uint32_t) chosen.brightness + 1);
    }
    if (chosen.sleep != prefs.sleep) {
        settings_write_number("sleep", (uint32_t) chosen.sleep + 1);
    }
    if (chosen.dark != prefs.dark) {
        settings_write_number("dark", (uint32_t) chosen.dark + 1);
    }
    if (chosen.orientation != prefs.orientation) {
        settings_write_number("orientation", (uint32_t) chosen.orientation + 1);
    }
    if (chosen.night != prefs.night) {
        settings_write_number("night", (uint32_t) chosen.night + 1);
    }
    if (chosen.night_from != prefs.night_from) {
        settings_write_number("night_from", (uint32_t) chosen.night_from + 1);
    }
    if (chosen.night_to != prefs.night_to) {
        settings_write_number("night_to", (uint32_t) chosen.night_to + 1);
    }
    prefs = chosen;
    ui_set_prefs(&prefs);
    ESP_LOGI(TAG,
             "preferences: volume %d, brightness %d, sleep %d, dark %d, orientation %d, sensitivity %d, night %d "
             "%d-%d",
             prefs.volume, prefs.brightness, prefs.sleep, prefs.dark, prefs.orientation, prefs.sensitivity, prefs.night,
             prefs.night_from, prefs.night_to);
}

static void change_prefs(void)
{
    home_prefs_t chosen;
    if (ui_take_prefs(&chosen)) {
        apply_prefs(chosen);
    }
}

static void capitals(const char *in, char *out, size_t capacity)
{
    size_t used = 0;
    while (*in && used + 3 < capacity) {
        unsigned char lead = (unsigned char) in[0];
        if (lead < 0x80) {
            out[used++] = (char) (lead >= 'a' && lead <= 'z' ? lead - 32 : lead);
            in++;
            continue;
        }
        if ((lead & 0xE0) != 0xC0 || !in[1]) {
            out[used++] = (char) lead;
            in++;
            continue;
        }
        unsigned code = ((lead & 0x1Fu) << 6) | ((unsigned char) in[1] & 0x3Fu);
        if (code >= 0xE0 && code <= 0xFE && code != 0xF7) {
            code -= 0x20;
        } else if (((code >= 0x100 && code <= 0x137) || (code >= 0x14A && code <= 0x177)) && (code & 1)) {
            code -= 1;
        } else if (((code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E)) && !(code & 1)) {
            code -= 1;
        }
        out[used++] = (char) (0xC0 | (code >> 6));
        out[used++] = (char) (0x80 | (code & 0x3F));
        in += 2;
    }
    out[used] = 0;
}

static bool is_strip(const char *name)
{
    return strstr(name, "pásek") || strstr(name, "Pásek") || strstr(name, "strip") || strstr(name, "Strip") ||
           strstr(name, "LED");
}

static int compare_devices(const void *left, const void *right)
{
    const home_device_t *a = left;
    const home_device_t *b = right;
    int room = strcmp(a->room, b->room);
    return room ? room : strcmp(a->name, b->name);
}

static void update_home(int64_t now)
{
    static int64_t last;
    static EXTERNAL_RAM home_data_t data;
    static EXTERNAL_RAM ikea_device_t found[HOME_DEVICES];
    if (now - last < HOME_INTERVAL_MS) {
        return;
    }
    last = now;
    memset(&data, 0, sizeof(data));
    int code = 0;
    data.weather = weather_current(&data.celsius, &code);
    data.sky = code;
    if (data.weather) {
        capitals(meteo_sky(code), data.sky_text, sizeof(data.sky_text));
    }
    printer_brief(data.printer, sizeof(data.printer));
    calendar_next(data.event, sizeof(data.event));
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    data.reminders = reminders_pending(NULL, 0);
    xSemaphoreGive(reminder_lock);
    data.todos = lists_open_todos();
    next_alarm_text(data.alarm, sizeof(data.alarm));
    switch (ikea_state()) {
    case IKEA_UNPAIRED:
        data.hub = HOME_HUB_OFFER;
        break;
    case IKEA_PAIRING:
        data.hub = HOME_HUB_PAIRING;
        break;
    case IKEA_READY:
        data.hub = HOME_HUB_READY;
        break;
    default:
        data.hub = HOME_HUB_HIDDEN;
        break;
    }
    int count = ikea_devices(found, HOME_DEVICES);
    for (int i = 0; i < count; i++) {
        home_device_t *device = &data.devices[i];
        strlcpy(device->id, found[i].id, sizeof(device->id));
        strlcpy(device->name, found[i].name[0] ? found[i].name : "bez názvu", sizeof(device->name));
        capitals(found[i].room, device->room, sizeof(device->room));
        device->kind = !found[i].light ? HOME_OUTLET : is_strip(found[i].name) ? HOME_STRIP : HOME_LIGHT;
        device->on = found[i].on;
        device->reachable = found[i].reachable;
        device->color = found[i].color;
        device->white = found[i].white;
        device->level = found[i].level > 0 ? found[i].level : 0;
    }
    qsort(data.devices, (size_t) count, sizeof(home_device_t), compare_devices);
    data.device_count = count;
    data.prefs = prefs;
    char address[20];
    net_address(address, sizeof(address));
    snprintf(data.info, sizeof(data.info), "Jarvis %s · Wi-Fi %d dBm · %s", esp_app_get_description()->version,
             net_signal(), address);
    ui_home_update(&data);
}

#if CONFIG_HEAP_TRACING_STANDALONE
#define TRACE_RECORDS 3000

static void trace_heap(const char *argument)
{
    static heap_trace_record_t *records;
    if (strcmp(argument, "start") == 0) {
        if (!records) {
            records = heap_caps_calloc(TRACE_RECORDS, sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM);
            if (!records || heap_trace_init_standalone(records, TRACE_RECORDS) != ESP_OK) {
                return;
            }
        }
        heap_trace_start(HEAP_TRACE_LEAKS);
    } else if (strcmp(argument, "dump") == 0) {
        heap_trace_stop();
        heap_trace_dump_caps(MALLOC_CAP_INTERNAL);
    }
}
#endif

static void on_console(void)
{
    char line[CONSOLE_LINE];
    if (!console_take(line, sizeof(line))) {
        return;
    }
    char *argument = strchr(line, ' ');
    if (argument) {
        *argument++ = 0;
    } else {
        argument = line + strlen(line);
    }
    bool on = strcmp(argument, "on") == 0;
    if (strcmp(line, "ask") == 0 && argument[0] && state == STATE_IDLE) {
        strlcpy(opening, argument, sizeof(opening));
        start_session(SESSION_TEXT);
    } else if (strcmp(line, "mute") == 0) {
        audio_set_muted(on);
    } else if (strcmp(line, "dry") == 0) {
        dry = on;
    } else if (strcmp(line, "turn") == 0 && argument[0] >= '0' && argument[0] <= '3') {
        home_prefs_t chosen = prefs;
        chosen.orientation = argument[0] - '0';
        apply_prefs(chosen);
    } else if (strcmp(line, "tap") == 0 || strcmp(line, "hold") == 0) {
        int x = 0;
        int y = 0;
        if (sscanf(argument, "%d %d", &x, &y) == 2) {
            ui_touch(x, y, line[0] == 't' ? 90 : 900);
        }
    } else if (strcmp(line, "touch") == 0) {
        app_post(APP_EVENT_TOUCH);
    } else if (strcmp(line, "wake") == 0) {
        wake_set_armed(false);
        wake_clear_capture();
        wake_set_capture(true);
        app_post(APP_EVENT_WAKE);
#if CONFIG_HEAP_TRACING_STANDALONE
    } else if (strcmp(line, "heap") == 0) {
        trace_heap(argument);
#endif
    } else if (strcmp(line, "remind") == 0 && net_time_valid()) {
        char *text = NULL;
        long seconds = strtol(argument, &text, 10);
        if (seconds > 0 && text && *text == ' ') {
            xSemaphoreTake(reminder_lock, portMAX_DELAY);
            reminders_add(time(NULL) + seconds, text + 1);
            xSemaphoreGive(reminder_lock);
            save_reminders();
        }
    }
    ESP_LOGI(TAG, "console: %s, state %d, muted %d, dry %d", line, (int) state, (int) audio_muted(), (int) dry);
}

extern const uint8_t alarm_chime_start[] asm("_binary_alarm_pcm_start");
extern const uint8_t alarm_chime_end[] asm("_binary_alarm_pcm_end");

static char tokens[API_TOKENS][API_TOKEN_LENGTH + 1];
static char pairing_code[8];
static int64_t pairing_until;
static int pairing_attempts;
static int64_t restart_at;
static bool ringing;
static bool ring_waiting;
static int64_t ring_since;
static int64_t ring_last;
static int ring_restore_volume = -1;
static alarm_t ring_alarm;

static void load_tokens(void)
{
    memset(tokens, 0, sizeof(tokens));
    settings_read_blob(NVS_APP_TOKENS, tokens, sizeof(tokens));
    for (int i = 0; i < API_TOKENS; i++) {
        tokens[i][API_TOKEN_LENGTH] = 0;
        if (strlen(tokens[i]) != API_TOKEN_LENGTH) {
            memset(tokens[i], 0, sizeof(tokens[i]));
        }
    }
}

static void save_tokens(void)
{
    settings_write_blob(NVS_APP_TOKENS, tokens, sizeof(tokens));
    api_set_tokens(tokens);
}

static void stop_pairing(void)
{
    if (pairing_code[0]) {
        pairing_code[0] = 0;
        ui_show_code(NULL);
    }
}

static void start_ringing(const alarm_t *alarm)
{
    char clock_text[8];
    snprintf(clock_text, sizeof(clock_text), "%d:%02d", alarm->hour, alarm->minute);
    ESP_LOGI(TAG, "alarm %u rings at %s", (unsigned) alarm->id, clock_text);
    stop_pairing();
    ringing = true;
    ring_waiting = false;
    ring_alarm = *alarm;
    ring_since = now_ms();
    ring_last = 0;
    if (prefs.volume < ALARM_MIN_VOLUME) {
        ring_restore_volume = prefs.volume;
        audio_set_volume(ALARM_MIN_VOLUME);
    }
    ui_show_alarm(clock_text, alarm->label);
}

static void stop_ringing(bool greet)
{
    if (!ringing) {
        return;
    }
    ringing = false;
    audio_flush();
    if (ring_restore_volume >= 0) {
        audio_set_volume(ring_restore_volume);
        ring_restore_volume = -1;
    }
    ui_show_alarm(NULL, NULL);
    ESP_LOGI(TAG, "alarm stopped%s", greet ? ", greeting" : "");
    if (greet && state == STATE_IDLE) {
        char label[ALARM_LABEL + 16] = "";
        if (ring_alarm.label[0]) {
            snprintf(label, sizeof(label), " „%s“", ring_alarm.label);
        }
        snprintf(opening, sizeof(opening), ALARM_GREETING, label);
        start_session(SESSION_TEXT);
    }
}

static void watch_alarms(int64_t now)
{
    if (net_time_valid() && !ringing && !ring_waiting) {
        alarm_t due;
        if (lists_due_alarm(time(NULL), &due)) {
            ring_alarm = due;
            ring_waiting = true;
        }
    }
    if (ring_waiting && state == STATE_IDLE) {
        start_ringing(&ring_alarm);
    }
    if (!ringing) {
        return;
    }
    if (now - ring_since > ALARM_RING_LIMIT_MS) {
        stop_ringing(false);
        return;
    }
    if (!audio_muted() && !audio_playing() && now - ring_last >= ALARM_GAP_MS) {
        audio_play((const int16_t *) alarm_chime_start, (size_t) (alarm_chime_end - alarm_chime_start) / sizeof(int16_t));
        audio_finish();
        ring_last = now;
    }
}

static void next_alarm_text(char *out, size_t capacity)
{
    static const char *const SHORT_DAYS[] = {"ne", "po", "út", "st", "čt", "pá", "so"};
    out[0] = 0;
    if (!net_time_valid()) {
        return;
    }
    time_t now = time(NULL);
    time_t when = 0;
    alarm_t alarm;
    if (!lists_next_alarm(now, &alarm, &when)) {
        return;
    }
    struct tm local;
    localtime_r(&when, &local);
    if (when - now < 24 * 60 * 60) {
        snprintf(out, capacity, "%d:%02d", local.tm_hour, local.tm_min);
    } else {
        snprintf(out, capacity, "%s %d:%02d", SHORT_DAYS[local.tm_wday], local.tm_hour, local.tm_min);
    }
}

static const char *state_name(void)
{
    static const char *const NAMES[] = {"offline", "idle", "connecting", "listening", "speaking", "error"};
    return ringing ? "ringing" : NAMES[state];
}

static char *json_reply(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static void api_error(api_call_t *call, int status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    call->status = status;
    call->reply = json_reply(root);
}

static cJSON *prefs_json(void)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "volume", prefs.volume);
    cJSON_AddNumberToObject(item, "brightness", prefs.brightness);
    cJSON_AddNumberToObject(item, "sleep", prefs.sleep);
    cJSON_AddNumberToObject(item, "dark", prefs.dark);
    cJSON_AddNumberToObject(item, "orientation", prefs.orientation);
    cJSON_AddNumberToObject(item, "sensitivity", prefs.sensitivity);
    cJSON_AddBoolToObject(item, "night", prefs.night);
    cJSON_AddNumberToObject(item, "night_from", prefs.night_from);
    cJSON_AddNumberToObject(item, "night_to", prefs.night_to);
    return item;
}

static cJSON *state_json(void)
{
    const settings_t *settings = settings_get();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "J-WALL Display");
    cJSON_AddStringToObject(root, "model", "Waveshare ESP32-S3-Touch-LCD-4B");
    cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "state", state_name());
    cJSON_AddNumberToObject(root, "uptime", (double) (esp_timer_get_time() / 1000000));
    cJSON_AddBoolToObject(root, "muted", audio_muted());
    cJSON_AddBoolToObject(root, "lamp", ui_lamp());
    cJSON_AddNumberToObject(root, "revision", lists_revision() + reminders_revision);
    cJSON *network = cJSON_AddObjectToObject(root, "network");
    char address[20];
    net_address(address, sizeof(address));
    cJSON_AddStringToObject(network, "ssid", settings->ssid);
    cJSON_AddNumberToObject(network, "rssi", net_signal());
    cJSON_AddStringToObject(network, "ip", address);
    cJSON *memory = cJSON_AddObjectToObject(root, "memory");
    cJSON_AddNumberToObject(memory, "internal", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(memory, "external", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON_AddItemToObject(root, "prefs", prefs_json());
    cJSON *stored = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddStringToObject(stored, "voice", settings->voice);
    cJSON_AddStringToObject(stored, "place", settings->place);
    cJSON_AddStringToObject(stored, "home", settings->home);
    cJSON_AddStringToObject(stored, "transport", settings->transport);
    cJSON_AddBoolToObject(stored, "search", settings->search);
    cJSON_AddBoolToObject(stored, "search_key", settings->search_key[0] != 0);
    cJSON_AddBoolToObject(stored, "printer", settings->printer_code[0] != 0);
    int calendars = 0;
    for (int i = 0; i < SETTINGS_CALENDARS; i++) {
        calendars += settings->calendars[i][0] != 0;
    }
    cJSON_AddNumberToObject(stored, "calendars", calendars);
    cJSON *hub = cJSON_AddObjectToObject(root, "hub");
    static const char *const HUB_STATES[] = {
        [IKEA_SEARCHING] = "searching", [IKEA_NOT_FOUND] = "missing", [IKEA_UNPAIRED] = "unpaired",
        [IKEA_PAIRING] = "pairing", [IKEA_READY] = "ready",
    };
    cJSON_AddStringToObject(hub, "state", HUB_STATES[ikea_state()]);
    cJSON_AddNumberToObject(hub, "devices", ikea_device_count());
    cJSON_AddNumberToObject(root, "todos_open", lists_open_todos());
    time_t when = 0;
    alarm_t alarm;
    if (net_time_valid() && lists_next_alarm(time(NULL), &alarm, &when)) {
        cJSON *next = cJSON_AddObjectToObject(root, "next_alarm");
        cJSON_AddNumberToObject(next, "id", alarm.id);
        cJSON_AddNumberToObject(next, "at", (double) when);
        cJSON_AddStringToObject(next, "label", alarm.label);
    }
    return root;
}

static bool read_int(const cJSON *body, const char *key, int low, int high, int *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, key);
    if (!item) {
        return true;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        return low == 0 && high >= 1;
    }
    if (!cJSON_IsNumber(item) || item->valueint < low || item->valueint > high) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static void api_prefs(api_call_t *call, const cJSON *body)
{
    home_prefs_t chosen = prefs;
    bool valid = read_int(body, "volume", 0, 100, &chosen.volume) &&
                 read_int(body, "brightness", 10, 100, &chosen.brightness) &&
                 read_int(body, "sleep", 0, HOME_SLEEP_CHOICES - 1, &chosen.sleep) &&
                 read_int(body, "dark", 0, 1, &chosen.dark) && read_int(body, "orientation", 0, 3, &chosen.orientation) &&
                 read_int(body, "sensitivity", 0, 2, &chosen.sensitivity) && read_int(body, "night", 0, 1, &chosen.night) &&
                 read_int(body, "night_from", 0, 24 * 60 - 1, &chosen.night_from) &&
                 read_int(body, "night_to", 0, 24 * 60 - 1, &chosen.night_to);
    if (!valid) {
        api_error(call, 400, "bad preference");
        return;
    }
    apply_prefs(chosen);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "prefs", prefs_json());
    call->reply = json_reply(root);
}

static void api_reminders(api_call_t *call, uint32_t id)
{
    if (call->method == API_DELETE && id) {
        xSemaphoreTake(reminder_lock, portMAX_DELAY);
        bool cancelled = reminders_cancel(id);
        xSemaphoreGive(reminder_lock);
        if (!cancelled) {
            api_error(call, 404, "no such reminder");
            return;
        }
        save_reminders();
    } else if (call->method != API_GET || id) {
        api_error(call, 404, "not found");
        return;
    }
    static reminder_t pending[REMINDERS_MAX];
    xSemaphoreTake(reminder_lock, portMAX_DELAY);
    int count = reminders_pending(pending, REMINDERS_MAX);
    xSemaphoreGive(reminder_lock);
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "reminders");
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", pending[i].id);
        cJSON_AddNumberToObject(item, "due", (double) pending[i].due);
        cJSON_AddStringToObject(item, "text", pending[i].text);
        cJSON_AddItemToArray(list, item);
    }
    call->reply = json_reply(root);
}

static void api_pair(api_call_t *call, const cJSON *body)
{
    int64_t now = now_ms();
    if (strcmp(call->path, "/api/pair/start") == 0) {
        if (ringing || state != STATE_IDLE) {
            api_error(call, 409, "busy");
            return;
        }
        snprintf(pairing_code, sizeof(pairing_code), "%06u", (unsigned) (esp_random() % 1000000));
        pairing_until = now + PAIRING_MS;
        pairing_attempts = 0;
        ui_show_code(pairing_code);
        ESP_LOGI(TAG, "pairing code %s is on the display", pairing_code);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "seconds", PAIRING_MS / 1000);
        call->reply = json_reply(root);
        return;
    }
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(body, "code");
    if (!pairing_code[0]) {
        api_error(call, 409, "no pairing in progress");
        return;
    }
    if (++pairing_attempts > PAIRING_ATTEMPTS) {
        stop_pairing();
        api_error(call, 429, "too many attempts");
        return;
    }
    if (!cJSON_IsString(code) || strcmp(code->valuestring, pairing_code) != 0) {
        api_error(call, 401, "wrong code");
        return;
    }
    int slot = 0;
    while (slot < API_TOKENS && tokens[slot][0]) {
        slot++;
    }
    if (slot == API_TOKENS) {
        memmove(tokens[0], tokens[1], sizeof(tokens) - sizeof(tokens[0]));
        slot = API_TOKENS - 1;
    }
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < API_TOKEN_LENGTH; i++) {
        tokens[slot][i] = HEX[esp_random() & 0xF];
    }
    tokens[slot][API_TOKEN_LENGTH] = 0;
    save_tokens();
    stop_pairing();
    ESP_LOGI(TAG, "phone paired into slot %d", slot);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "token", tokens[slot]);
    cJSON_AddItemToObject(root, "state", state_json());
    call->reply = json_reply(root);
}

static void api_forget(api_call_t *call)
{
    for (int i = 0; i < API_TOKENS; i++) {
        if (tokens[i][0] && strcmp(tokens[i], call->token) == 0) {
            memset(tokens[i], 0, sizeof(tokens[i]));
        }
    }
    save_tokens();
    call->reply = json_reply(cJSON_CreateObject());
}

static void handle_api(api_call_t *call)
{
    const char *path = call->path;
    cJSON *body = call->body ? cJSON_Parse(call->body) : NULL;
    if (call->body && !body) {
        api_error(call, 400, "invalid JSON");
        return;
    }
    if (strcmp(path, "/api/hello") == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "name", "J-WALL Display");
        cJSON_AddStringToObject(root, "model", "Waveshare ESP32-S3-Touch-LCD-4B");
        cJSON_AddStringToObject(root, "version", esp_app_get_description()->version);
        cJSON_AddBoolToObject(root, "paired", call->authorized);
        call->reply = json_reply(root);
    } else if (strncmp(path, "/api/pair", 9) == 0 && call->method == API_POST) {
        api_pair(call, body);
    } else if (!call->authorized) {
        api_error(call, 401, "pair first");
    } else if (lists_handle(call)) {
    } else if (strcmp(path, "/api/state") == 0) {
        call->reply = json_reply(state_json());
    } else if (strcmp(path, "/api/prefs") == 0 && call->method == API_PUT) {
        api_prefs(call, body);
    } else if (strcmp(path, "/api/settings") == 0 && call->method == API_PUT) {
        char error[48];
        if (call->body && settings_store(call->body, error, sizeof(error))) {
            restart_at = now_ms() + RESTART_DELAY_MS;
            cJSON *root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "restart", true);
            call->reply = json_reply(root);
        } else {
            api_error(call, 400, call->body ? error : "settings missing");
        }
    } else if (strncmp(path, "/api/reminders", 14) == 0) {
        api_reminders(call, path[14] == '/' ? (uint32_t) strtoul(path + 15, NULL, 10) : 0);
    } else if (strcmp(path, "/api/lamp") == 0 && call->method == API_POST) {
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(body, "on");
        ui_set_lamp(cJSON_IsBool(on) ? cJSON_IsTrue(on) : !ui_lamp());
        call->reply = json_reply(state_json());
    } else if (strcmp(path, "/api/say") == 0 && call->method == API_POST) {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(body, "text");
        if (!cJSON_IsString(text) || !text->valuestring[0] || strlen(text->valuestring) > SAY_LIMIT) {
            api_error(call, 400, "text missing or too long");
        } else if (state != STATE_IDLE || ringing) {
            api_error(call, 409, "busy");
        } else {
            snprintf(opening, sizeof(opening), PHONE_MESSAGE, text->valuestring);
            start_session(SESSION_TEXT);
            call->reply = json_reply(cJSON_CreateObject());
        }
    } else if (strcmp(path, "/api/alarm/stop") == 0 && call->method == API_POST) {
        stop_ringing(false);
        call->reply = json_reply(state_json());
    } else if (strcmp(path, "/api/restart") == 0 && call->method == API_POST) {
        restart_at = now_ms() + RESTART_DELAY_MS;
        call->reply = json_reply(cJSON_CreateObject());
    } else if (strcmp(path, "/api/forget") == 0 && call->method == API_POST) {
        api_forget(call);
    } else {
        api_error(call, 404, "not found");
    }
    cJSON_Delete(body);
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
        if (ringing) {
            wake_set_capture(false);
            wake_clear_capture();
            stop_ringing(true);
        } else if (dry) {
            ESP_LOGI(TAG, "wake word ignored");
            wake_set_capture(false);
            wake_clear_capture();
            wake_set_armed(true);
        } else if (state == STATE_IDLE) {
            start_session(SESSION_VOICE);
        } else if (!in_session) {
            wake_set_capture(false);
            wake_clear_capture();
            wake_set_armed(true);
        }
        break;
    case APP_EVENT_TOUCH:
        if (ringing) {
            stop_ringing(true);
        } else if (pairing_code[0]) {
            stop_pairing();
        } else if (ui_lamp()) {
            ui_set_lamp(false);
        } else if (state == STATE_IDLE) {
            start_session(SESSION_TOUCH);
        } else if (in_session || state == STATE_ERROR) {
            end_announcement(announced);
            go_idle();
        }
        break;
    case APP_EVENT_SESSION_READY:
        if (state == STATE_CONNECTING) {
            last_activity = now_ms();
            enter(STATE_LISTENING);
            if (session_kind == SESSION_ANNOUNCE || session_kind == SESSION_TEXT) {
                live_send_text(opening);
            }
        }
        break;
    case APP_EVENT_MODEL_AUDIO:
        if (in_session) {
            uplink_enabled = false;
            wake_set_capture(false);
            if (uplinked) {
                uplinked = false;
                live_send_stream_end();
            }
            if (announcing && !announced) {
                announced = true;
                xSemaphoreTake(reminder_lock, portMAX_DELAY);
                reminders_attempted(announcing, time(NULL));
                xSemaphoreGive(reminder_lock);
                save_reminders();
            }
            if (delivering_unheard) {
                delivering_unheard = false;
                xSemaphoreTake(reminder_lock, portMAX_DELAY);
                reminders_drop_unheard();
                xSemaphoreGive(reminder_lock);
                save_reminders();
            }
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
        if (awaiting_answer && heard_letters >= 2) {
            end_announcement(true);
        }
        break;
    case APP_EVENT_SESSION_CLOSED:
        on_session_closed();
        break;
    case APP_EVENT_PAIR_REQUEST:
        ikea_request_pairing();
        break;
    case APP_EVENT_HUB_CHANGED:
        break;
    case APP_EVENT_TALK:
        if (state == STATE_IDLE) {
            start_session(SESSION_TOUCH);
        }
        break;
    case APP_EVENT_LAMP:
        ui_set_lamp(!ui_lamp());
        break;
    case APP_EVENT_PREFS:
        change_prefs();
        break;
    case APP_EVENT_HUB_PAIRED:
        settings_write_hub_token(ikea_token());
        break;
    case APP_EVENT_REMINDERS_CHANGED:
        save_reminders();
        break;
    case APP_EVENT_LISTS_CHANGED:
        lists_save();
        break;
    case APP_EVENT_API:
        api_serve(handle_api);
        break;
    case APP_EVENT_CONSOLE:
        on_console();
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
    int draws = 0;
    int draw_average = 0;
    int draw_longest = 0;
    display_take_stats(&draws, &draw_average, &draw_longest);
    ESP_LOGI(TAG,
             "state %d, wifi %d dBm, mic peak %d in %u frames, display %d fps at %d us, ui %d draws at %d us (longest "
             "%d), heap %u internal (lowest %u, block %u), %u external",
             (int) state, net_signal(), wake_take_peak(), (unsigned) (frames - last_frames), frames_per_second,
             frame_microseconds, draws, draw_average, draw_longest,
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    last = now;
    last_frames = frames;
}

static void show_printer(int64_t now)
{
    static int64_t last;
    static char shown[40];
    char summary[40];
    if (now - last < STATUS_INTERVAL_MS) {
        return;
    }
    last = now;
    printer_summary(summary, sizeof(summary));
    if (strcmp(summary, shown) != 0) {
        strcpy(shown, summary);
        ui_set_status(summary);
    }
}

static void track_noise(void)
{
    noise_floor += ((float) wake_microphone_level() - noise_floor) * NOISE_SMOOTHING;
}

static bool speech_heard(void)
{
    int level = wake_microphone_level();
    return level > VOICE_LEVEL && level > (int) noise_floor + SPEECH_ABOVE_NOISE;
}

static void acknowledge(int64_t now)
{
    if (ack_pending) {
        if (speech_heard()) {
            ack_loud = now;
        }
        if (heard_any || now - ack_since > ACK_WINDOW_MS) {
            ESP_LOGI(TAG, "no acknowledgement, the speaker kept talking (background %d)", (int) noise_floor);
            ack_pending = false;
            return;
        }
        if (now - ack_loud < ACK_QUIET_MS) {
            return;
        }
        ack_pending = false;
        if (audio_muted() || prefs.volume == 0) {
            return;
        }
        uint32_t choice = esp_random() % (sizeof(ACKNOWLEDGEMENTS) / sizeof(ACKNOWLEDGEMENTS[0]));
        const clip_t *clip = &ACKNOWLEDGEMENTS[choice];
        ESP_LOGI(TAG, "acknowledging with phrase %u after %d ms, background %d", (unsigned) choice, (int) (now - ack_since),
                 (int) noise_floor);
        wake_set_capture(false);
        wake_clear_capture();
        audio_play((const int16_t *) clip->start, (size_t) (clip->end - clip->start) / sizeof(int16_t));
        audio_finish();
        acking = true;
        ack_ended = 0;
        return;
    }
    if (!acking) {
        return;
    }
    if (audio_playing()) {
        return;
    }
    if (!ack_ended) {
        ack_ended = now;
        return;
    }
    if (now - ack_ended >= ECHO_GUARD_MS) {
        audio_stats_t stats;
        audio_take_stats(&stats);
        acking = false;
        wake_clear_capture();
        wake_set_capture(true);
        last_activity = now;
        last_voice = now;
    }
}

static void supervise(void)
{
    int64_t now = now_ms();
    watch_alarms(now);
    if (pairing_code[0] && now > pairing_until) {
        stop_pairing();
    }
    if (restart_at && now >= restart_at) {
        esp_restart();
    }
    report(now);
    show_printer(now);
    update_home(now);
    if (tool_busy) {
        last_activity = now;
    }
    switch (state) {
    case STATE_IDLE:
        track_noise();
        if (!ringing) {
            announce_due();
        }
        break;
    case STATE_CONNECTING:
        acknowledge(now);
        if (now - state_since <= CONNECT_TIMEOUT_MS) {
            break;
        }
        if (connect_again("no answer")) {
            enter(STATE_CONNECTING);
        } else {
            fail("Gemini se neozval. Zkontroluj Wi-Fi a klíč.");
        }
        break;
    case STATE_LISTENING:
        acknowledge(now);
        if (!acking && wake_microphone_level() > VOICE_LEVEL) {
            last_voice = now;
        }
        if (heard_any && thinking != (now - last_voice > THINKING_AFTER_MS)) {
            thinking = !thinking;
            ui_set_mode(thinking ? UI_THINKING : UI_LISTENING, NULL);
        }
        if (now - last_activity >
                (awaiting_answer ? ANSWER_TIMEOUT_MS : replied ? FOLLOW_UP_TIMEOUT_MS : FIRST_TURN_TIMEOUT_MS) ||
            now - session_since > SESSION_LIMIT_MS) {
            go_idle();
        }
        break;
    case STATE_SPEAKING:
        if (audio_playing()) {
            last_model_audio = now;
        } else if (turn_complete || now - last_model_audio > STALLED_REPLY_TIMEOUT_MS) {
            audio_stats_t stats;
            audio_take_stats(&stats);
            ESP_LOGI(TAG, "reply played: %u ms of speech, longest pause in arrival %u ms, %u dropouts",
                     (unsigned) stats.played_ms, (unsigned) stats.longest_gap_ms, (unsigned) stats.underruns);
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
    reminder_lock = xSemaphoreCreateMutex();

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
    load_prefs();
    ui_set_prefs(&prefs);
    if (audio_init(prefs.volume) != ESP_OK) {
        ESP_LOGE(TAG, "audio failed to start");
    } else {
        checkpoint("audio");
        if (wake_start() != ESP_OK) {
            ESP_LOGE(TAG, "wake word failed to start");
        } else {
            wake_set_sensitivity(prefs.sensitivity);
        }
    }
    checkpoint("wake word");
    if (!configured) {
        ui_set_mode(UI_SETUP_NEEDED, NULL);
        vTaskDelete(NULL);
        return;
    }

    key_mark = 2166136261U;
    for (const char *c = settings_get()->api_key; *c; c++) {
        key_mark = (key_mark ^ (unsigned char) *c) * 16777619U;
    }
    key_mark |= 1;
    search_allowed = settings_read_number(SEARCH_REFUSED) != key_mark;
    const live_callbacks_t callbacks = {.heard = on_heard, .spoken = on_spoken, .tool = on_tool};
    live_init(&callbacks);
    xTaskCreatePinnedToCoreWithCaps(uplink_task, "uplink", 6144, NULL, 5, NULL, 0, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(tool_task, "tools", 12288, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
    static uint8_t stored_reminders[sizeof(reminder_t) * REMINDERS_MAX + 16];
    reminders_load(stored_reminders, settings_read_blob(NVS_REMINDERS, stored_reminders, sizeof(stored_reminders)));
    lists_init();
    load_tokens();
    show_reminders();
    ui_set_mode(UI_OFFLINE, NULL);
    net_start(settings_get()->ssid, settings_get()->password);
    api_start(esp_app_get_description()->version);
    api_set_tokens(tokens);
    checkpoint("network");
    static char hub_token[1536];
    settings_read_hub_token(hub_token, sizeof(hub_token));
    ikea_start(hub_token);
    printer_start();
    calendar_start();
    weather_start();
    control_start();
    console_start();

    vTaskPrioritySet(NULL, CONTROL_PRIORITY);
    for (;;) {
        app_event_t event;
        int wait = ack_pending || acking ? ACK_LOOP_WAIT_MS : LOOP_WAIT_MS;
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(wait)) == pdTRUE) {
            handle(event);
        }
        supervise();
    }
}
