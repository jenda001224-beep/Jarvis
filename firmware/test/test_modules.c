#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bambu.h"
#include "cJSON.h"
#include "geo.h"
#include "meteo.h"
#include "proto.h"
#include "reminders.h"
#include "tavily.h"
#include "todos.h"
#include "alarms.h"
#include "cloudmail.h"
#include "countdown.h"
#include "dav.h"
#include "gemini.h"
#include "notices.h"
#include "sound.h"
#include "updateinfo.h"
#include <math.h>

static const char FULL_REPORT[] =
    "{\"print\":{\"bed_target_temper\":60.0,\"bed_temper\":59.6,\"command\":\"push_status\",\"fail_reason\":\"0\","
    "\"gcode_state\":\"RUNNING\",\"hms\":[],\"layer_num\":42,\"mc_percent\":37,\"mc_print_stage\":\"2\","
    "\"mc_remaining_time\":83,\"nozzle_temper\":219.7,\"print_error\":0,\"stg\":[2,14,1],\"stg_cur\":0,"
    "\"subtask_name\":\"Držák na sluchátka\",\"total_layer_num\":210,\"ams\":{\"ams\":[{\"id\":\"0\",\"tray\":["
    "{\"id\":\"0\",\"tray_type\":\"PLA\"}]}]},\"sequence_id\":\"2021\"}}";

static void test_printer(void)
{
    bambu_status_t status;
    bambu_status_reset(&status);
    assert(!bambu_merge_report(&status, "{\"info\":{\"command\":\"get_version\"}}", 34));
    assert(!status.known);
    assert(bambu_merge_report(&status, FULL_REPORT, strlen(FULL_REPORT)));
    assert(status.known && strcmp(status.state, "RUNNING") == 0 && status.percent == 37);
    assert(status.remaining_minutes == 83 && status.layer == 42 && status.total_layers == 210);
    assert(status.nozzle == 220 && status.bed == 60 && status.stage == 0 && status.alert_count == 0);
    assert(strcmp(status.job, "Držák na sluchátka") == 0);

    char line[48];
    bambu_summary(&status, line, sizeof(line));
    assert(strcmp(line, "TISK 37 % · 1:23") == 0);

    const char *delta = "{\"print\":{\"mc_percent\":\"38\",\"mc_remaining_time\":\"81\",\"command\":\"push_status\"}}";
    assert(bambu_merge_report(&status, delta, strlen(delta)));
    assert(status.percent == 38 && status.remaining_minutes == 81 && strcmp(status.state, "RUNNING") == 0);

    const char *clog = "{\"print\":{\"gcode_state\":\"PAUSE\",\"stg_cur\":35,\"print_error\":50348044,"
                       "\"hms\":[{\"attr\":50331904,\"code\":65543},{\"attr\":201327360,\"code\":131073}]}}";
    assert(bambu_merge_report(&status, clog, strlen(clog)));
    assert(strcmp(status.state, "PAUSE") == 0 && status.alert_count == 2 && status.print_error == 50348044);
    char code[BAMBU_CODE_LENGTH];
    bambu_alert_code(&status.alerts[0], code);
    assert(strcmp(code, "0300010000010007") == 0);
    bambu_alert_code(&status.alerts[1], code);
    assert(strcmp(code, "0C00030000020001") == 0);
    bambu_error_code(status.print_error, code);
    assert(strcmp(code, "0300400C") == 0);
    bambu_error_code(117473286, code);
    assert(strcmp(code, "07008006") == 0);
    assert(strcmp(bambu_stage_text(35), "pozastaveno: ucpaná tryska") == 0);
    assert(bambu_stage_text(12) == NULL && bambu_stage_text(-1) == NULL && bambu_stage_text(255) == NULL);
    bambu_summary(&status, line, sizeof(line));
    assert(strcmp(line, "TISK STOJÍ") == 0);

    bambu_text_t texts[2] = {{"0300010000010007", "The heatbed temperature is abnormal."}, {"0300400C", ""}};
    char *described = bambu_describe(&status, texts, 2);
    cJSON *root = cJSON_Parse(described);
    assert(root);
    assert(strcmp(cJSON_GetObjectItem(root, "state")->valuestring, "pozastavená") == 0);
    assert(strcmp(cJSON_GetObjectItem(root, "activity")->valuestring, "pozastaveno: ucpaná tryska") == 0);
    assert(cJSON_GetObjectItem(root, "percent")->valueint == 38);
    const cJSON *problems = cJSON_GetObjectItem(root, "problems");
    assert(cJSON_GetArraySize(problems) == 3);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(problems, 1), "description")->valuestring,
                  "The heatbed temperature is abnormal.") == 0);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(problems, 1), "severity")->valuestring, "fatální") == 0);
    printf("printer tool answer: %s\n", described);
    cJSON_Delete(root);
    free(described);

    const char *done = "{\"print\":{\"gcode_state\":\"FINISH\",\"hms\":[],\"print_error\":0,\"stg_cur\":-1}}";
    assert(bambu_merge_report(&status, done, strlen(done)));
    assert(status.alert_count == 0 && status.print_error == 0 && !bambu_active(&status));
    assert(bambu_summary(&status, line, sizeof(line)) == 0 && line[0] == 0);
    described = bambu_describe(&status, NULL, 0);
    assert(strstr(described, "tisk dokončen") && strstr(described, "last_job") && !strstr(described, "percent"));
    free(described);

    char address[40];
    char serial[24];
    const char *notify = "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nServer: UPnP/1.0\r\n"
                         "Location: 10.0.0.42\r\nNT: urn:bambulab-com:device:3dprinter:1\r\n"
                         "USN: 03919C000000000\r\nCache-Control: max-age=1800\r\nDevModel.bambu.com: N2S\r\n"
                         "DevName.bambu.com: A1\r\n\r\n";
    assert(bambu_parse_announcement(notify, strlen(notify), address, sizeof(address), serial, sizeof(serial)));
    assert(strcmp(address, "10.0.0.42") == 0 && strcmp(serial, "03919C000000000") == 0);
    const char *answer = "HTTP/1.1 200 OK\r\nlocation: http://10.0.0.43:80/x\r\nST: urn:bambulab-com:device:3dprinter:1"
                         "\r\nusn: 0309DA000000001\r\n\r\n";
    assert(bambu_parse_announcement(answer, strlen(answer), address, sizeof(address), serial, sizeof(serial)));
    assert(strcmp(address, "10.0.0.43") == 0 && strcmp(serial, "0309DA000000001") == 0);
    const char *bare = "NOTIFY * HTTP/1.1\r\nNT: urn:bambulab-com:device:3dprinter:1\r\nUSN: 0309DA000000002\r\n\r\n";
    assert(bambu_parse_announcement(bare, strlen(bare), address, sizeof(address), serial, sizeof(serial)));
    assert(address[0] == 0 && strcmp(serial, "0309DA000000002") == 0);
    const char *other = "NOTIFY * HTTP/1.1\r\nLocation: http://10.0.0.1:5000/desc.xml\r\nNT: upnp:rootdevice\r\n"
                        "USN: uuid:1234\r\n\r\n";
    assert(!bambu_parse_announcement(other, strlen(other), address, sizeof(address), serial, sizeof(serial)));

    char text[BAMBU_TEXT_LENGTH];
    const char *lookup = "{\"result\":0,\"t\":1791646424,\"ver\":202609231145,\"data\":{\"device_hms\":{\"ver\":1,"
                         "\"en\":[{\"ecode\":\"0300010000010007\",\"intro\":\"The heatbed temperature is abnormal; "
                         "the sensor may have an open circuit.\"}]}}}";
    assert(bambu_parse_lookup(lookup, strlen(lookup), text, sizeof(text)));
    assert(strncmp(text, "The heatbed temperature is abnormal;", 36) == 0);
    const char *unknown = "{\"result\":201,\"t\":1791646424,\"ver\":0,\"msg\":\"\"}";
    assert(!bambu_parse_lookup(unknown, strlen(unknown), text, sizeof(text)));
    assert(!bambu_parse_lookup("<html>", 6, text, sizeof(text)));
}

static void test_reminders(void)
{
    reminder_t item;
    reminder_t list[REMINDERS_MAX];
    assert(!reminders_due(1000, &item));
    uint32_t later = reminders_add(2000, "Je čas vyrazit k doktorovi.");
    uint32_t sooner = reminders_add(1500, "Vypněte troubu.");
    assert(later && sooner && later != sooner);
    assert(!reminders_due(1499, &item));
    assert(reminders_due(1500, &item) && item.id == sooner);
    assert(reminders_due(5000, &item) && item.id == sooner);
    assert(reminders_pending(list, REMINDERS_MAX) == 2 && list[0].id == sooner && list[1].id == later);
    assert(reminders_unheard(list, REMINDERS_MAX) == 0);

    reminders_defer(sooner, 5090);
    assert(reminders_due(5000, &item) && item.id == later);
    reminders_attempted(sooner, 5010);
    assert(reminders_unheard(list, REMINDERS_MAX) == 1 && list[0].id == sooner && list[0].attempts == 1);
    assert(reminders_pending(list, REMINDERS_MAX) == 1 && list[0].id == later);
    assert(reminders_due(5010 + REMINDER_RETRY_SECONDS, &item) && item.id == sooner);
    for (int i = 1; i < REMINDER_ATTEMPTS; i++) {
        reminders_attempted(sooner, 6000 + i);
    }
    assert(reminders_cancel(later));
    assert(!reminders_due(999999, &item));
    assert(reminders_unheard(NULL, 0) == 1);

    uint8_t blob[4096];
    size_t length = reminders_save(blob, sizeof(blob));
    assert(length == reminders_blob_size() && length <= sizeof(reminder_t) * REMINDERS_MAX + 16);
    assert(reminders_drop_unheard() == 1 && reminders_unheard(NULL, 0) == 0);
    reminders_load(blob, length - 1);
    assert(reminders_unheard(NULL, 0) == 0);
    blob[0] ^= 0xFF;
    reminders_load(blob, length);
    assert(reminders_unheard(NULL, 0) == 0);
    blob[0] ^= 0xFF;
    reminders_load(blob, length);
    assert(reminders_unheard(list, REMINDERS_MAX) == 1 && strcmp(list[0].text, "Vypněte troubu.") == 0);
    assert(reminders_drop_unheard() == 1);

    char longer[400];
    memset(longer, 0, sizeof(longer));
    for (int i = 0; i < 180; i++) {
        memcpy(longer + i * 2, "ž", 2);
    }
    uint32_t ids[REMINDERS_MAX + 1];
    for (int i = 0; i <= REMINDERS_MAX; i++) {
        ids[i] = reminders_add(9000 + i, longer);
    }
    assert(ids[REMINDERS_MAX - 1] != 0 && ids[REMINDERS_MAX] == 0);
    assert(reminders_pending(list, REMINDERS_MAX) == REMINDERS_MAX);
    assert(strlen(list[0].text) == REMINDER_TEXT - 2 && (unsigned char) list[0].text[REMINDER_TEXT - 3] == 0xBE);
    for (int i = 0; i < REMINDERS_MAX; i++) {
        assert(reminders_cancel(ids[i]));
    }
    assert(!reminders_cancel(ids[0]) && !reminders_cancel(0));
}

static void test_travel(void)
{
    char encoded[64];
    geo_encode("Antala Staška 1670/80, Praha 4", encoded, sizeof(encoded));
    assert(strcmp(encoded, "Antala%20Sta%C5%A1ka%201670%2F80%2C%20Praha%204") == 0);
    char tight[8];
    assert(geo_encode("a b c d", tight, sizeof(tight)) == 5 && strcmp(tight, "a%20b") == 0);

    double latitude = 0;
    double longitude = 0;
    const char *place = "[{\"place_id\":1,\"lat\":\"50.0843218\",\"lon\":\"14.4248309\",\"name\":\"El Emir\"}]";
    assert(geo_parse_place(place, strlen(place), &latitude, &longitude));
    assert(latitude > 50.0843 && latitude < 50.0844 && longitude > 14.4248 && longitude < 14.4249);
    assert(!geo_parse_place("[]", 2, &latitude, &longitude));
    assert(!geo_parse_place("{\"error\":\"x\"}", 13, &latitude, &longitude));

    double seconds = 0;
    double meters = 0;
    const char *route = "{\"code\":\"Ok\",\"routes\":[{\"legs\":[],\"duration\":2025.8,\"distance\":7188}],"
                        "\"waypoints\":[]}";
    assert(geo_parse_route(route, strlen(route), &seconds, &meters));
    assert(seconds > 2025 && seconds < 2026 && meters == 7188);
    const char *none = "{\"code\":\"NoRoute\",\"routes\":[]}";
    assert(!geo_parse_route(none, strlen(none), &seconds, &meters));
}

static void test_weather(void)
{
    double latitude = 0;
    double longitude = 0;
    char name[32];
    const char *place = "{\"results\":[{\"id\":3067696,\"name\":\"Praha\",\"latitude\":50.08804,"
                        "\"longitude\":14.42076,\"country\":\"Česko\"}],\"generationtime_ms\":0.15}";
    assert(meteo_parse_place(place, strlen(place), &latitude, &longitude, name, sizeof(name)));
    assert(strcmp(name, "Praha") == 0 && latitude > 50.08 && longitude < 14.43);
    assert(!meteo_parse_place("{\"generationtime_ms\":0.1}", 25, &latitude, &longitude, name, sizeof(name)));

    const char *forecast =
        "{\"latitude\":50.08,\"current\":{\"time\":\"2026-10-11T08:30\",\"temperature_2m\":9.6,"
        "\"apparent_temperature\":6.7,\"weather_code\":2,\"wind_speed_10m\":13.0},"
        "\"daily\":{\"time\":[\"2026-10-11\",\"2026-10-12\",\"2026-10-13\"],\"weather_code\":[61,80,45],"
        "\"temperature_2m_max\":[14.3,14.3,15.2],\"temperature_2m_min\":[8.8,6.6,2.4],"
        "\"precipitation_probability_max\":[28,21,2],\"wind_speed_10m_max\":[15.9,12.3,4.1]}}";
    char *described = meteo_describe(forecast, strlen(forecast), "Praha");
    cJSON *root = cJSON_Parse(described);
    assert(root);
    const cJSON *now = cJSON_GetObjectItem(root, "now");
    assert(strcmp(cJSON_GetObjectItem(now, "sky")->valuestring, "polojasno") == 0);
    assert(cJSON_GetObjectItem(now, "celsius")->valueint == 10 && cJSON_GetObjectItem(now, "feels_like")->valueint == 7);
    const cJSON *days = cJSON_GetObjectItem(root, "days");
    assert(cJSON_GetArraySize(days) == 3);
    const cJSON *third = cJSON_GetArrayItem(days, 2);
    assert(strcmp(cJSON_GetObjectItem(third, "weekday")->valuestring, "úterý") == 0);
    assert(strcmp(cJSON_GetObjectItem(third, "sky")->valuestring, "mlha") == 0);
    assert(cJSON_GetObjectItem(third, "min_celsius")->valueint == 2);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(days, 0), "weekday")->valuestring, "dnes") == 0);
    printf("weather tool answer: %s\n", described);
    cJSON_Delete(root);
    free(described);
    assert(meteo_describe("{\"error\":true}", 14, "Praha") == NULL);
    assert(strcmp(meteo_sky(95), "bouřka") == 0 && strcmp(meteo_sky(12345), "neznámé počasí") == 0);
}

static void test_text_message(void)
{
    char *message = proto_build_text("Zpráva od systému: „test“");
    cJSON *root = cJSON_Parse(message);
    const cJSON *content = cJSON_GetObjectItem(root, "clientContent");
    const cJSON *turn = cJSON_GetArrayItem(cJSON_GetObjectItem(content, "turns"), 0);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(content, "turnComplete")));
    assert(strcmp(cJSON_GetObjectItem(turn, "role")->valuestring, "user") == 0);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(turn, "parts"), 0), "text")->valuestring,
                  "Zpráva od systému: „test“") == 0);
    cJSON_Delete(root);
    proto_free(message);
}

static void test_search(void)
{
    char *request = tavily_request("bazén Step Praha otevírací doba");
    cJSON *sent = cJSON_Parse(request);
    assert(strcmp(cJSON_GetObjectItem(sent, "query")->valuestring, "bazén Step Praha otevírací doba") == 0);
    assert(strcmp(cJSON_GetObjectItem(sent, "search_depth")->valuestring, "basic") == 0);
    assert(cJSON_GetObjectItem(sent, "max_results")->valueint == 5);
    cJSON_Delete(sent);
    free(request);

    char long_text[1400] = "";
    while (strlen(long_text) + sizeof("žluť ") < sizeof(long_text)) {
        strcat(long_text, "žluť ");
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "query", "bazén Step");
    cJSON_AddStringToObject(response, "answer", "Bazén Step je dnes otevřený do 21:30.");
    cJSON *results = cJSON_AddArrayToObject(response, "results");
    for (int i = 0; i < 7; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "title", "Otevírací doba");
        cJSON_AddStringToObject(item, "url", "https://example.cz/bazen");
        cJSON_AddStringToObject(item, "content", i == 0 ? long_text : i == 1 ? "" : "Po–Pá 6:00–21:30");
        cJSON_AddNumberToObject(item, "score", 0.8);
        cJSON_AddItemToArray(results, item);
    }
    char *raw = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    char *summary = tavily_summarize(raw, strlen(raw));
    free(raw);
    cJSON *root = cJSON_Parse(summary);
    assert(root);
    assert(strcmp(cJSON_GetObjectItem(root, "answer")->valuestring, "Bazén Step je dnes otevřený do 21:30.") == 0);
    const cJSON *found = cJSON_GetObjectItem(root, "results");
    assert(cJSON_GetArraySize(found) == 5);
    const char *trimmed = cJSON_GetObjectItem(cJSON_GetArrayItem(found, 0), "content")->valuestring;
    size_t length = strlen(trimmed);
    assert(length <= 600 + 3 && length > 590);
    assert(strcmp(trimmed + length - 3, "…") == 0);
    assert(((unsigned char) trimmed[length - 4] & 0xC0) != 0xC0);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(found, 1), "content")->valuestring, "Po–Pá 6:00–21:30") == 0);
    printf("search tool answer: %.160s…\n", summary);
    cJSON_Delete(root);
    free(summary);

    assert(tavily_summarize("{\"results\":[]}", 14) == NULL);
    assert(tavily_summarize("not json", 8) == NULL);
}

static void test_todos(void)
{
    uint32_t milk = todos_add("  koupit mléko ", 100);
    uint32_t bills = todos_add("zaplatit složenky", 50);
    assert(milk && bills && milk != bills);
    assert(todos_add("   ", 10) == 0);
    todo_t list[TODOS_MAX];
    assert(todos_list(list, TODOS_MAX) == 2);
    assert(list[0].id == bills && strcmp(list[1].text, "koupit mléko") == 0);
    assert(todos_find("Koupit Mléko") == milk);
    assert(todos_set_done(bills, true) && todos_open_count() == 1);
    assert(todos_list(list, TODOS_MAX) == 2 && list[0].id == milk && list[1].done);
    assert(todos_rename(milk, "koupit ovesné mléko") && todos_find("koupit ovesné mléko") == milk);
    char blob[sizeof(list) + 64];
    size_t length = todos_save(blob, sizeof(blob));
    assert(length == todos_blob_size());
    assert(todos_clear_done() == 1 && todos_open_count() == 1);
    todos_load(blob, length);
    assert(todos_list(list, TODOS_MAX) == 2);
    assert(todos_remove(milk) && !todos_remove(milk));
    char longest[200];
    memset(longest, 0, sizeof(longest));
    for (int i = 0; i < 60; i++) {
        strcat(longest, "č");
    }
    uint32_t cut = todos_add(longest, 1);
    int count = todos_list(list, TODOS_MAX);
    for (int i = 0; i < count; i++) {
        if (list[i].id == cut) {
            assert(strlen(list[i].text) < TODO_TEXT && strlen(list[i].text) % 2 == 0);
        }
    }
    while (todos_add("úkol", 2)) {
    }
    assert(todos_list(list, TODOS_MAX) == TODOS_MAX);
}

static time_t prague(int year, int month, int day, int hour, int minute)
{
    struct tm moment = {.tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = hour,
                        .tm_min = minute, .tm_isdst = -1};
    return mktime(&moment);
}

static void test_alarms(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    uint8_t weekdays = 0x1F;
    uint32_t work = alarms_add(6, 30, weekdays, "Do práce");
    uint32_t once = alarms_add(9, 0, 0, "Jednou");
    assert(work && once && alarms_add(24, 0, 0, "x") == 0);
    alarm_t alarm;
    time_t when = 0;
    time_t friday_evening = prague(2026, 10, 23, 20, 0);
    assert(alarms_next(friday_evening, &alarm, &when) && alarm.id == once);
    assert(when == prague(2026, 10, 24, 9, 0));
    assert(alarms_remove(once));
    assert(alarms_next(friday_evening, &alarm, &when) && alarm.id == work);
    assert(when == prague(2026, 10, 26, 6, 30));
    time_t monday = prague(2026, 10, 26, 6, 30) + 20;
    assert(alarms_due(monday, &alarm) && alarm.id == work);
    assert(!alarms_due(monday + 30, &alarm));
    assert(!alarms_due(prague(2026, 10, 25, 6, 30), &alarm));
    uint32_t single = alarms_add(7, 15, 0, "");
    assert(alarms_due(prague(2026, 10, 27, 7, 15), &alarm) && alarm.id == single);
    assert(alarms_get(single, &alarm) && !alarm.enabled);
    assert(alarms_update(single, 7, 20, 0, "Znovu", true));
    assert(alarms_get(single, &alarm) && alarm.enabled && alarm.fired_day == -1);
    alarm_t list[ALARMS_MAX];
    assert(alarms_list(list, ALARMS_MAX) == 2 && list[0].id == work && list[1].hour == 7);
    char blob[sizeof(list) + 64];
    size_t length = alarms_save(blob, sizeof(blob));
    assert(alarms_remove(work) && alarms_list(list, ALARMS_MAX) == 1);
    alarms_load(blob, length);
    assert(alarms_list(list, ALARMS_MAX) == 2);
    assert(alarms_day_bit(1) == 0x01 && alarms_day_bit(0) == 0x40);
}

static void test_timers(void)
{
    uint32_t pasta = countdown_add(1000, 600, "Těstoviny");
    uint32_t tea = countdown_add(1000, 180, "");
    assert(pasta && tea && countdown_add(1000, 0, "") == 0 && countdown_add(1000, 90000, "") == 0);
    countdown_t list[COUNTDOWN_MAX];
    assert(countdown_list(list, COUNTDOWN_MAX) == 2 && list[0].id == tea && list[1].id == pasta);
    assert(countdown_list(list, 1) == 1 && list[0].id == tea);
    assert(countdown_find("těstoviny") == pasta && countdown_find("TESTOVINY") == pasta);
    assert(countdown_find("Těstoviny") == pasta && countdown_find("čaj") == 0);
    countdown_t done;
    assert(!countdown_due(180000, &done));
    assert(countdown_due(181000, &done) && done.id == tea && !countdown_due(181000, &done));
    assert(countdown_cancel(pasta) && !countdown_cancel(pasta));
    for (int i = 0; i < COUNTDOWN_MAX; i++) {
        assert(countdown_add(0, i + 1, "x"));
    }
    assert(countdown_add(0, 5, "x") == 0);
    assert(countdown_clear() == COUNTDOWN_MAX && countdown_list(list, COUNTDOWN_MAX) == 0);
    char text[16];
    countdown_text(582000, text, sizeof(text));
    assert(strcmp(text, "9:42") == 0);
    countdown_text(581001, text, sizeof(text));
    assert(strcmp(text, "9:42") == 0);
    countdown_text(3723000, text, sizeof(text));
    assert(strcmp(text, "1:02:03") == 0);
    countdown_text(-5, text, sizeof(text));
    assert(strcmp(text, "0:00") == 0);
}

static void test_gemini(void)
{
    char *request = gemini_text_request("Pravidla \"x\"", "Otázka\nřádek", 100);
    cJSON *root = cJSON_Parse(request);
    assert(root);
    const cJSON *system = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "systemInstruction"), "parts"), 0);
    assert(strcmp(cJSON_GetObjectItem(system, "text")->valuestring, "Pravidla \"x\"") == 0);
    const cJSON *content = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "contents"), 0);
    assert(strcmp(cJSON_GetObjectItem(content, "role")->valuestring, "user") == 0);
    const cJSON *part = cJSON_GetArrayItem(cJSON_GetObjectItem(content, "parts"), 0);
    assert(strcmp(cJSON_GetObjectItem(part, "text")->valuestring, "Otázka\nřádek") == 0);
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "generationConfig"), "maxOutputTokens")->valueint == 100);
    cJSON_Delete(root);
    free(request);

    static const char GOOD[] = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"úvaha\",\"thought\":true},"
                               "{\"text\":\"- Má \"},{\"text\":\"psa\"}],\"role\":\"model\"},\"finishReason\":\"STOP\"}]}";
    char *answer = gemini_text_answer(GOOD, strlen(GOOD));
    assert(answer && strcmp(answer, "- Má psa") == 0);
    free(answer);
    static const char CUT[] = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"- Má\"}]},\"finishReason\":\"MAX_TOKENS\"}]}";
    assert(!gemini_text_answer(CUT, strlen(CUT)));
    static const char BLOCKED[] = "{\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}";
    assert(!gemini_text_answer(BLOCKED, strlen(BLOCKED)));
    assert(!gemini_text_answer("nonsense", 8));
}

static void test_notices(void)
{
    notices_clear();
    assert(notices_add(1000, "mac", "", true) == 0);
    uint32_t first = notices_add(1000, "mac", "Claude dokončil\npráci", true);
    assert(first && notices_add(1030, "mac", "Claude dokončil\npráci", true) == first);
    uint32_t second = notices_add(1100, "tiskárna", "Tisk je hotový", false);
    assert(second != first && notices_unread() == 2);
    notice_t list[NOTICES_MAX];
    assert(notices_list(list, NOTICES_MAX) == 2 && list[0].id == second && list[1].id == first);
    assert(strcmp(list[1].text, "Claude dokončil práci") == 0 && !list[0].speak && list[1].speak);
    char joined[256];
    assert(notices_join(joined, sizeof(joined), false, false) > 0);
    assert(strcmp(joined, "Claude dokončil práci; Tisk je hotový") == 0 && notices_unread() == 2);
    assert(notices_join(joined, sizeof(joined), true, true) > 0 && strcmp(joined, "Claude dokončil práci") == 0);
    assert(notices_unread() == 1);
    assert(notices_join(joined, sizeof(joined), true, true) == 0 && joined[0] == 0);
    assert(notices_join(joined, 5, false, false) == 0);
    notices_mark_all();
    assert(notices_unread() == 0);
    for (int i = 0; i < NOTICES_MAX + 3; i++) {
        char text[32];
        snprintf(text, sizeof(text), "zpráva %d", i);
        assert(notices_add(2000 + i * 100, "x", text, true));
    }
    assert(notices_list(list, NOTICES_MAX) == NOTICES_MAX && strcmp(list[0].text, "zpráva 18") == 0);
    assert(strcmp(list[NOTICES_MAX - 1].text, "zpráva 3") == 0);
    char longer[NOTICE_TEXT * 2];
    size_t size = 0;
    while (size + 2 < sizeof(longer) - 1) {
        longer[size++] = (char) 0xC5;
        longer[size++] = (char) 0xA1;
    }
    longer[size] = 0;
    assert(notices_add(99999, "x", longer, true));
    notices_list(list, 1);
    assert(strlen(list[0].text) % 2 == 0 && strlen(list[0].text) < NOTICE_TEXT);
    notices_clear();
}

static void test_printer_events(void)
{
    assert(bambu_event("RUNNING", "FINISH") == BAMBU_EVENT_FINISHED);
    assert(bambu_event("PAUSE", "FINISH") == BAMBU_EVENT_FINISHED);
    assert(bambu_event("RUNNING", "FAILED") == BAMBU_EVENT_FAILED);
    assert(bambu_event("PREPARE", "FAILED") == BAMBU_EVENT_FAILED);
    assert(bambu_event("RUNNING", "PAUSE") == BAMBU_EVENT_PAUSED);
    assert(bambu_event("", "FINISH") == BAMBU_EVENT_NONE);
    assert(bambu_event("FINISH", "FINISH") == BAMBU_EVENT_NONE);
    assert(bambu_event("IDLE", "RUNNING") == BAMBU_EVENT_NONE);
    assert(bambu_event("FINISH", "FAILED") == BAMBU_EVENT_NONE);
}

static void test_rain_and_sun(void)
{
    static const char DRY[] = "{\"minutely_15\":{\"time\":[\"a\"],\"precipitation\":[0,0,0.02,0]}}";
    static const char SOON[] = "{\"minutely_15\":{\"precipitation\":[0,0,0.4,1.0]}}";
    static const char NOW[] = "{\"minutely_15\":{\"precipitation\":[0.2,0.1]}}";
    assert(meteo_rain_in(DRY, strlen(DRY)) == -1);
    assert(meteo_rain_in(SOON, strlen(SOON)) == 30);
    assert(meteo_rain_in(NOW, strlen(NOW)) == 0);
    assert(meteo_rain_in("{}", 2) == -1);
    static const char DAY[] = "{\"daily\":{\"sunrise\":[\"2026-10-31T06:47\"],\"sunset\":[\"2026-10-31T16:42\"]}}";
    int rise = 0;
    int set = 0;
    assert(meteo_sun(DAY, strlen(DAY), &rise, &set) && rise == 407 && set == 1002);
    assert(!meteo_sun("{\"daily\":{\"sunrise\":[\"bad\"]}}", 29, &rise, &set));
    static const char WEEK[] = "{\"daily\":{\"time\":[\"2026-10-31\",\"2026-11-01\",\"bad\"],\"weather_code\":[3,61,0],"
                               "\"temperature_2m_max\":[12.4,9.6,1],\"temperature_2m_min\":[5.5,-0.4,1],"
                               "\"precipitation_probability_max\":[10,80,0]}}";
    meteo_day_t days[4];
    assert(meteo_days(WEEK, strlen(WEEK), days, 4) == 2);
    assert(days[0].year == 2026 && days[0].month == 10 && days[0].day == 31 && days[0].code == 3 && days[0].high == 12 && days[0].low == 6);
    assert(days[1].day == 1 && days[1].rain == 80 && days[1].low == 0);
    assert(meteo_days(WEEK, strlen(WEEK), days, 1) == 1);
    assert(meteo_days("{}", 2, days, 4) == 0);
}

static char dav_collected[1024];
static int dav_ends;

static void dav_on_data(void *context, const char *text, size_t length)
{
    (void) context;
    strncat(dav_collected, text, length);
}

static void dav_on_end(void *context)
{
    (void) context;
    dav_ends++;
    strcat(dav_collected, "|");
}

static void test_caldav(void)
{
    static const char PRINCIPAL[] =
        "<?xml version='1.0' encoding='UTF-8'?><multistatus xmlns='DAV:'><response><href>/</href><propstat><prop>"
        "<current-user-principal><href>/123456/principal/</href></current-user-principal></prop>"
        "<status>HTTP/1.1 200 OK</status></propstat></response></multistatus>";
    char href[DAV_HREF];
    assert(dav_find_href(PRINCIPAL, strlen(PRINCIPAL), "current-user-principal", href, sizeof(href)));
    assert(strcmp(href, "/123456/principal/") == 0);
    assert(!dav_find_href(PRINCIPAL, strlen(PRINCIPAL), "calendar-home-set", href, sizeof(href)));
    static const char HOME[] =
        "<D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/123456/principal/</D:href><D:propstat><D:prop>"
        "<C:calendar-home-set xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n  <D:href>https://p42-caldav.icloud.com:443/123456/calendars/?a=1&amp;b=2</D:href>\n"
        "</C:calendar-home-set></D:prop></D:propstat></D:response></D:multistatus>";
    assert(dav_find_href(HOME, strlen(HOME), "calendar-home-set", href, sizeof(href)));
    assert(strcmp(href, "https://p42-caldav.icloud.com:443/123456/calendars/?a=1&b=2") == 0);
    char origin[64];
    dav_origin(href, origin, sizeof(origin));
    assert(strcmp(origin, "https://p42-caldav.icloud.com:443") == 0);
    char url[256];
    dav_join_url(origin, "/123456/calendars/home/", url, sizeof(url));
    assert(strcmp(url, "https://p42-caldav.icloud.com:443/123456/calendars/home/") == 0);
    dav_join_url(origin, "https://other.example/x/", url, sizeof(url));
    assert(strcmp(url, "https://other.example/x/") == 0);

    static const char LIST[] =
        "<multistatus xmlns=\"DAV:\">"
        "<response><href>/123456/calendars/</href><propstat><prop><resourcetype><collection/></resourcetype></prop></propstat></response>"
        "<response xmlns=\"DAV:\"><href>/123456/calendars/home/</href><propstat><prop><displayname>Domů &amp; rodina</displayname>"
        "<resourcetype xmlns=\"DAV:\"><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
        "<supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\"><comp name=\"VEVENT\"/></supported-calendar-component-set>"
        "</prop></propstat></response>"
        "<response><href>/123456/calendars/tasks/</href><propstat><prop><displayname>Připomínky</displayname>"
        "<resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
        "<supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\"><comp name=\"VTODO\"/></supported-calendar-component-set>"
        "</prop></propstat></response>"
        "<response><href>/123456/calendars/inbox/</href><propstat><prop><resourcetype><collection/><schedule-inbox xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype></prop></propstat></response>"
        "<response><href>/123456/calendars/work/</href><propstat><prop><displayname>Práce</displayname>"
        "<resourcetype><collection/><C:calendar xmlns:C=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype></prop></propstat></response>"
        "</multistatus>";
    dav_calendar_t calendars[4];
    assert(dav_parse_calendars(LIST, strlen(LIST), calendars, 4) == 2);
    assert(strcmp(calendars[0].name, "Domů & rodina") == 0 && strcmp(calendars[0].href, "/123456/calendars/home/") == 0);
    assert(strcmp(calendars[1].name, "Práce") == 0);
    assert(dav_parse_calendars(LIST, strlen(LIST), calendars, 1) == 1);

    static const char REPORT[] =
        "<multistatus xmlns=\"DAV:\"><response><href>/a.ics</href><propstat><prop>"
        "<calendar-data xmlns=\"urn:ietf:params:xml:ns:caldav\">BEGIN:VEVENT&#13;\nSUMMARY:A &amp; B &lt;x&gt; &#x10D;</calendar-data>"
        "</prop></propstat></response><response><href>/b.ics</href><propstat><prop>"
        "<C:calendar-data xmlns:C=\"urn:ietf:params:xml:ns:caldav\"><![CDATA[SUMMARY:<raw> ]] ok & done]]></C:calendar-data>"
        "<C:calendar-data/></prop></propstat></response></multistatus>";
    const char *expected = "BEGIN:VEVENT\r\nSUMMARY:A & B <x> č|SUMMARY:<raw> ]] ok & done|";
    for (size_t step = 1; step <= 7; step += 3) {
        dav_collected[0] = 0;
        dav_ends = 0;
        static dav_stream_t stream;
        dav_stream_begin(&stream, dav_on_data, dav_on_end, NULL);
        for (size_t at = 0; at < sizeof(REPORT) - 1; at += step) {
            size_t size = sizeof(REPORT) - 1 - at < step ? sizeof(REPORT) - 1 - at : step;
            dav_stream_feed(&stream, REPORT + at, size);
        }
        dav_stream_finish(&stream);
        assert(strcmp(dav_collected, expected) == 0 && dav_ends == 2);
    }

    char *query = dav_query_body(1793404800, 1794614400);
    assert(strstr(query, "start=\"20261031T000000Z\"") && strstr(query, "end=\"20261114T000000Z\""));
    free(query);

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    dav_event_t event = {
        .uid = "abc-jarvis",
        .title = "Doktor, kontrola; zuby \\ a ještě jeden hodně dlouhý název události, aby se musel řádek zalomit přesně podle pravidel",
        .place = "Olgy Havlové 2874/2, Praha",
        .start = 1793955600,
        .end = 1793959200,
        .stamp = 1793404800,
    };
    char *text = dav_build_event(&event);
    assert(strstr(text, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"));
    assert(strstr(text, "UID:abc-jarvis\r\n") && strstr(text, "DTSTAMP:20261031T000000Z\r\n"));
    assert(strstr(text, "DTSTART:20261106T090000Z\r\n") && strstr(text, "DTEND:20261106T100000Z\r\n"));
    assert(strstr(text, "SUMMARY:Doktor\\, kontrola\\; zuby \\\\ a"));
    assert(strstr(text, "LOCATION:Olgy Havlové 2874/2\\, Praha\r\n") && strstr(text, "END:VCALENDAR\r\n"));
    size_t octets = 0;
    for (const char *c = text; *c; c++) {
        if (*c == '\r') {
            assert(c[1] == '\n' && octets <= 75);
            octets = 0;
            c++;
        } else {
            octets++;
        }
    }
    assert(strstr(text, "\r\n "));
    free(text);
    event.all_day = true;
    event.place = NULL;
    event.start = 1793401200;
    event.end = 1793487600;
    text = dav_build_event(&event);
    assert(strstr(text, "DTSTART;VALUE=DATE:20261031\r\n") && strstr(text, "DTEND;VALUE=DATE:20261101\r\n") && !strstr(text, "LOCATION"));
    free(text);
    unsetenv("TZ");
    tzset();
}

static double rms_of(const int16_t *pcm, size_t count)
{
    double sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (double) pcm[i] * pcm[i];
    }
    return sqrt(sum / (double) count);
}

static void test_sound(void)
{
    enum { INPUT = 44100 };
    static int16_t in[INPUT];
    static int16_t out[INPUT];
    for (int i = 0; i < INPUT; i++) {
        in[i] = (int16_t) (10000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * i / 44100.0));
    }
    rate_t rate;
    rate_init(&rate, 44100, 24000);
    size_t produced = 0;
    for (int at = 0; at < INPUT; at += 1152) {
        int take = INPUT - at < 1152 ? INPUT - at : 1152;
        assert(rate_capacity(&rate, 1152) >= 627);
        produced += rate_process(&rate, in + at, (size_t) take, out + produced, (size_t) (INPUT - produced));
    }
    assert(produced >= 23998 && produced <= 24002);
    double level = rms_of(out + 1000, produced - 1000);
    assert(level > 6000 && level < 7500);
    double crossings = 0;
    for (size_t i = 1001; i < produced; i++) {
        crossings += (out[i - 1] < 0) != (out[i] < 0);
    }
    assert(crossings > 1900 && crossings < 2020);

    for (int i = 0; i < INPUT; i++) {
        in[i] = (int16_t) (10000.0 * sin(2.0 * 3.14159265358979 * 18000.0 * i / 44100.0));
    }
    rate_init(&rate, 44100, 24000);
    produced = rate_process(&rate, in, INPUT, out, INPUT);
    assert(rms_of(out + 1000, produced - 1000) < 1500);

    rate_init(&rate, 24000, 24000);
    assert(rate_process(&rate, in, 1000, out, INPUT) == 1000);
    rate_init(&rate, 16000, 24000);
    size_t raised = rate_process(&rate, in, 1600, out, INPUT);
    assert(raised >= 2398 && raised <= 2402);

    int16_t stereo[6] = {100, 300, -200, -400, 32000, 32000};
    sound_to_mono(stereo, 3, 2);
    assert(stereo[0] == 200 && stereo[1] == -300 && stereo[2] == 32000);

    static int16_t noise[48000];
    noise_t state;
    for (int kind = NOISE_WHITE; kind <= NOISE_BROWN; kind++) {
        noise_init(&state, 12345);
        noise_fill(&state, (noise_kind_t) kind, noise, 48000);
        noise_fill(&state, (noise_kind_t) kind, noise, 48000);
        double loud = rms_of(noise, 48000);
        assert(loud > 800 && loud < 12000);
        double steps = 0;
        for (int i = 1; i < 48000; i++) {
            steps += fabs((double) noise[i] - noise[i - 1]);
        }
        double roughness = steps / 48000.0 / loud;
        assert(kind == NOISE_WHITE ? roughness > 1.0 : kind == NOISE_PINK ? roughness > 0.2 && roughness < 1.4 : roughness < 0.3);
    }

    static const uint8_t MP3[] = {0, 0, 0xFF, 0xFB, 0x90, 0x64};
    static const uint8_t AAC[] = {0xFF, 0xF1, 0x50, 0x80};
    static const uint8_t TAGGED[] = {'I', 'D', '3', 4};
    static const uint8_t JUNK[] = {'<', 'h', 't', 'm', 'l', '>'};
    assert(sound_sniff(MP3, sizeof(MP3)) == SOUND_MP3 && sound_sniff(AAC, sizeof(AAC)) == SOUND_AAC);
    assert(sound_sniff(TAGGED, sizeof(TAGGED)) == SOUND_MP3 && sound_sniff(JUNK, sizeof(JUNK)) == SOUND_UNKNOWN);

    static const char RSS[] =
        "<rss><channel><title>Pořad</title><item><title><![CDATA[ D&#xED;l 12: Tom & Jerry&#33; ]]></title><description>x</description>"
        "<enclosure length=\"1\" type=\"audio/mpeg\" url=\"https://a.example/ep12.mp3?x=1&amp;y=2\"/></item>"
        "<item><title>Starší</title><enclosure url=\"https://a.example/ep11.mp3\"/></item></channel></rss>";
    char title[64];
    char url[128];
    assert(rss_first_item(RSS, strlen(RSS), title, sizeof(title), url, sizeof(url)));
    assert(strcmp(title, "Díl 12: Tom & Jerry!") == 0 && strcmp(url, "https://a.example/ep12.mp3?x=1&y=2") == 0);
    static const char NO_AUDIO[] = "<rss><item><title>A</title></item><item><enclosure url='b'/></item></rss>";
    assert(!rss_first_item(NO_AUDIO, strlen(NO_AUDIO), title, sizeof(title), url, sizeof(url)));
    assert(!rss_first_item("<rss></rss>", 11, title, sizeof(title), url, sizeof(url)));

    assert(strcmp(station_name(station_match("Český rozhlas Dvojka")), "Dvojka") == 0);
    assert(strcmp(station_name(station_match("RADIOŽURNÁL")), "Radiožurnál") == 0);
    assert(strcmp(station_name(station_match("evropu 2")), "Evropa 2") == 0);
    assert(station_match("Rádio Bonton") == -1);
}

static void test_chat_protocol(void)
{
    static const char FUNCTIONS[] =
        "[{\"name\":\"list\",\"description\":\"d\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{}}},"
        "{\"name\":\"add\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"x\":{\"type\":\"STRING\"}}}}]";
    char *tools = gemini_tools(FUNCTIONS);
    cJSON *root = cJSON_Parse(tools);
    const cJSON *declarations = cJSON_GetObjectItem(cJSON_GetArrayItem(root, 0), "functionDeclarations");
    assert(cJSON_GetArraySize(declarations) == 2);
    assert(!cJSON_GetObjectItem(cJSON_GetArrayItem(declarations, 0), "parameters"));
    assert(cJSON_GetObjectItem(cJSON_GetArrayItem(declarations, 1), "parameters"));
    cJSON_Delete(root);

    char *contents = gemini_append_turn(NULL, "user", "Ahoj");
    char *body = gemini_chat_request("Pravidla", contents, tools, 50);
    root = cJSON_Parse(body);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "contents")) == 1);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "tools")) == 1);
    assert(cJSON_GetObjectItem(root, "systemInstruction"));
    cJSON_Delete(root);
    free(body);
    free(tools);

    static const char CALL[] =
        "{\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[{\"functionCall\":{\"name\":\"add\",\"args\":{\"x\":\"mléko\"}},"
        "\"thoughtSignature\":\"sig\"},{\"text\":\"úvaha\",\"thought\":true}]}}]}";
    gemini_turn_t turn;
    assert(gemini_parse_turn(CALL, strlen(CALL), &turn));
    assert(turn.call_count == 1 && strcmp(turn.calls[0].name, "add") == 0 && strstr(turn.calls[0].args, "mléko"));
    assert(!turn.text && strstr(turn.content, "thoughtSignature"));
    char *with_call = gemini_append_content(contents, turn.content);
    char *results[1] = {strdup("{\"result\":\"ok\"}")};
    char *with_answer = gemini_append_responses(with_call, turn.calls, results, 1);
    root = cJSON_Parse(with_answer);
    assert(cJSON_GetArraySize(root) == 3);
    const cJSON *echoed = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(root, 1), "parts"), 0);
    assert(strcmp(cJSON_GetObjectItem(echoed, "thoughtSignature")->valuestring, "sig") == 0);
    const cJSON *reply = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(root, 2), "parts"), 0), "functionResponse");
    assert(strcmp(cJSON_GetObjectItem(reply, "name")->valuestring, "add") == 0);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(reply, "response"), "result")->valuestring, "ok") == 0);
    cJSON_Delete(root);
    free(results[0]);
    results[0] = strdup("not json");
    char *wrapped = gemini_append_responses(NULL, turn.calls, results, 1);
    assert(strstr(wrapped, "not json"));
    free(wrapped);
    free(results[0]);
    free(with_call);
    free(with_answer);
    free(contents);
    gemini_turn_free(&turn);

    static const char DONE[] = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Hotovo, \"},{\"text\":\"pane.\"}]}}]}";
    assert(gemini_parse_turn(DONE, strlen(DONE), &turn) && turn.call_count == 0 && strcmp(turn.text, "Hotovo, pane.") == 0);
    gemini_turn_free(&turn);
    assert(!gemini_parse_turn("{\"error\":{}}", 12, &turn));

    const char *names[] = {"cmd-a", "cmd-b"};
    char *lookup = cloud_lookup_body(names, 2);
    root = cJSON_Parse(lookup);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "records")) == 2);
    cJSON_Delete(root);
    free(lookup);
    char *modify = cloud_modify_body("forceReplace", "rep-a", "JarvisMail", 7, "AAA", NULL);
    root = cJSON_Parse(modify);
    const cJSON *operation = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "operations"), 0);
    assert(strcmp(cJSON_GetObjectItem(operation, "operationType")->valuestring, "forceReplace") == 0);
    const cJSON *record = cJSON_GetObjectItem(operation, "record");
    assert(strcmp(cJSON_GetObjectItem(record, "recordName")->valuestring, "rep-a") == 0);
    const cJSON *fields = cJSON_GetObjectItem(record, "fields");
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(fields, "seq"), "value")->valueint == 7);
    assert(strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(fields, "data"), "value")->valuestring, "AAA") == 0);
    assert(!cJSON_GetObjectItem(fields, "text"));
    cJSON_Delete(root);
    free(modify);
    modify = cloud_modify_body("create", NULL, "JarvisNotice", 1, NULL, "Tisk je hotový");
    assert(!strstr(modify, "recordName") && strstr(modify, "Tisk je hotový"));
    free(modify);
    char message[128];
    cloud_signature_message("2026-10-31T08:00:00Z", "aGFzaA==", "/database/1/x/records/lookup", message, sizeof(message));
    assert(strcmp(message, "2026-10-31T08:00:00Z:aGFzaA==:/database/1/x/records/lookup") == 0);
}

static void test_release(void)
{
    assert(release_compare("2.6.1", "2.6.1") == 0);
    assert(release_compare("2.7.0", "2.6.9") > 0);
    assert(release_compare("2.10.0", "2.9.9") > 0);
    assert(release_compare("2.6.1", "3.0.0") < 0);
    assert(release_compare("garbage", "0.0.1") < 0);
    static const char MANIFEST[] =
        "{\"name\":\"Jarvis\",\"version\":\"2.7.0\",\"config\":{\"offset\":9502720},\"builds\":[{\"chipFamily\":\"ESP32-S3\",\"parts\":["
        "{\"path\":\"firmware/bootloader.bin\",\"offset\":0,\"size\":22272,\"md5\":\"b868b1d7a5e69782ceb11560164ab0e4\"},"
        "{\"path\":\"firmware/jarvis.bin\",\"offset\":65536,\"size\":3000000,\"md5\":\"00112233445566778899aabbccddeeff\"}]}]}";
    release_t release;
    assert(release_parse(MANIFEST, strlen(MANIFEST), 0x10000, &release));
    assert(strcmp(release.version, "2.7.0") == 0 && strcmp(release.path, "firmware/jarvis.bin") == 0);
    assert(strcmp(release.md5, "00112233445566778899aabbccddeeff") == 0 && release.size == 3000000);
    assert(!release_parse(MANIFEST, strlen(MANIFEST), 0x20000, &release));
    assert(!release_parse("{}", 2, 0x10000, &release));
    assert(!release_parse("nonsense", 8, 0x10000, &release));
    static const char OLD[] = "{\"version\":\"2.6.1\",\"builds\":[{\"parts\":[{\"path\":\"firmware/jarvis.bin\",\"offset\":65536}]}]}";
    assert(release_parse(OLD, strlen(OLD), 0x10000, &release) && release.size == 0 && release.md5[0] == 0);
}

int main(void)
{
    test_printer();
    test_reminders();
    test_travel();
    test_weather();
    test_text_message();
    test_search();
    test_todos();
    test_alarms();
    test_timers();
    test_gemini();
    test_notices();
    test_printer_events();
    test_rain_and_sun();
    test_caldav();
    test_sound();
    test_chat_protocol();
    test_release();
    puts("modules: printer, reminders, travel, weather, text prompt, search, to-dos, alarms, timers, Gemini, notices, rain, CalDAV, sound, chat and releases pass");
    return 0;
}
