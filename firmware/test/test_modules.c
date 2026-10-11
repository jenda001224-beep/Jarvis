#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bambu.h"
#include "cJSON.h"
#include "geo.h"
#include "meteo.h"
#include "proto.h"
#include "reminders.h"

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

int main(void)
{
    test_printer();
    test_reminders();
    test_travel();
    test_weather();
    test_text_message();
    puts("modules: printer, reminders, travel, weather and text prompt pass");
    return 0;
}
