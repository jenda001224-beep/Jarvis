#include "bambu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define SERVICE "urn:bambulab-com:device:3dprinter"

typedef struct {
    const char *key;
    const char *text;
} named_t;

static const named_t STATES[] = {
    {"IDLE", "nečinná"},         {"PREPARE", "připravuje tisk"}, {"SLICING", "připravuje tisk"},
    {"RUNNING", "tiskne"},       {"PAUSE", "pozastavená"},       {"FINISH", "tisk dokončen"},
    {"FAILED", "tisk selhal"},   {"INIT", "startuje"},           {"OFFLINE", "odpojená"},
};

static const char *const STAGES[] = {
    [0] = "tiskne",
    [1] = "měří rovinnost podložky",
    [2] = "předehřívá podložku",
    [3] = "měří vibrace",
    [4] = "mění filament",
    [5] = "pauza vložená v G-kódu",
    [6] = "pozastaveno: došel filament",
    [7] = "nahřívá trysku",
    [8] = "kalibruje vytlačování",
    [9] = "skenuje podložku",
    [10] = "kontroluje první vrstvu",
    [11] = "zjišťuje typ podložky",
    [13] = "najíždí do výchozí polohy",
    [14] = "čistí trysku",
    [15] = "kontroluje teplotu extruderu",
    [16] = "pozastaveno uživatelem",
    [17] = "pozastaveno: spadl přední kryt tiskové hlavy",
    [19] = "kalibruje průtok",
    [20] = "pozastaveno: porucha teploty trysky",
    [21] = "pozastaveno: porucha teploty podložky",
    [22] = "vysouvá filament",
    [23] = "pozastaveno: motor ztratil kroky",
    [24] = "zavádí filament",
    [25] = "kalibruje hluk motorů",
    [26] = "pozastaveno: ztracené spojení s AMS",
    [27] = "pozastaveno: pomalý ventilátor chlazení trysky",
    [30] = "pozastaveno příkazem v G-kódu",
    [32] = "pozastaveno: tryska je obalená filamentem",
    [33] = "pozastaveno: chyba odřezávače filamentu",
    [34] = "pozastaveno: chyba první vrstvy",
    [35] = "pozastaveno: ucpaná tryska",
};

void bambu_status_reset(bambu_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->percent = -1;
    status->remaining_minutes = -1;
    status->stage = -1;
    status->nozzle = -1;
    status->bed = -1;
}

static bool read_number(const cJSON *object, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring[0]) {
        char *end = NULL;
        double value = strtod(item->valuestring, &end);
        if (end != item->valuestring && *end == 0) {
            *out = value;
            return true;
        }
    }
    return false;
}

static void read_int(const cJSON *object, const char *key, int *out)
{
    double value;
    if (read_number(object, key, &value) && value > -1000000 && value < 1000000) {
        *out = (int) (value < 0 ? value - 0.5 : value + 0.5);
    }
}

static void read_text(const cJSON *object, const char *key, char *out, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item)) {
        return;
    }
    size_t length = strlen(item->valuestring);
    if (length >= capacity) {
        length = capacity - 1;
        while (length > 0 && ((unsigned char) item->valuestring[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(out, item->valuestring, length);
    out[length] = 0;
}

static uint32_t unsigned_of(const cJSON *item)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0) {
        return 0;
    }
    return (uint32_t) item->valuedouble;
}

bool bambu_merge_report(bambu_status_t *status, const char *json, size_t length)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *print = cJSON_GetObjectItemCaseSensitive(root, "print");
    if (!cJSON_IsObject(print)) {
        cJSON_Delete(root);
        return false;
    }
    read_text(print, "gcode_state", status->state, sizeof(status->state));
    read_text(print, "subtask_name", status->job, sizeof(status->job));
    read_int(print, "mc_percent", &status->percent);
    read_int(print, "mc_remaining_time", &status->remaining_minutes);
    read_int(print, "layer_num", &status->layer);
    read_int(print, "total_layer_num", &status->total_layers);
    read_int(print, "stg_cur", &status->stage);
    read_int(print, "nozzle_temper", &status->nozzle);
    read_int(print, "bed_temper", &status->bed);
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(print, "print_error");
    if (cJSON_IsNumber(error)) {
        status->print_error = unsigned_of(error);
    }
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(print, "hms");
    if (cJSON_IsArray(alerts)) {
        status->alert_count = 0;
        const cJSON *alert;
        cJSON_ArrayForEach(alert, alerts)
        {
            if (status->alert_count >= BAMBU_MAX_ALERTS) {
                break;
            }
            uint32_t attr = unsigned_of(cJSON_GetObjectItemCaseSensitive(alert, "attr"));
            uint32_t code = unsigned_of(cJSON_GetObjectItemCaseSensitive(alert, "code"));
            if (attr || code) {
                status->alerts[status->alert_count].attr = attr;
                status->alerts[status->alert_count].code = code;
                status->alert_count++;
            }
        }
    }
    if (status->state[0]) {
        status->known = true;
    }
    cJSON_Delete(root);
    return true;
}

void bambu_alert_code(const bambu_alert_t *alert, char out[BAMBU_CODE_LENGTH])
{
    snprintf(out, BAMBU_CODE_LENGTH, "%08lX%08lX", (unsigned long) alert->attr, (unsigned long) alert->code);
}

void bambu_error_code(uint32_t print_error, char out[BAMBU_CODE_LENGTH])
{
    snprintf(out, BAMBU_CODE_LENGTH, "%08lX", (unsigned long) print_error);
}

const char *bambu_alert_severity(const bambu_alert_t *alert)
{
    switch (alert->code >> 16) {
    case 1:
        return "fatální";
    case 2:
        return "vážná";
    case 3:
        return "běžná";
    case 4:
        return "informace";
    default:
        return "neznámá";
    }
}

const char *bambu_state_text(const char *state)
{
    for (size_t i = 0; i < sizeof(STATES) / sizeof(STATES[0]); i++) {
        if (strcmp(state, STATES[i].key) == 0) {
            return STATES[i].text;
        }
    }
    return NULL;
}

const char *bambu_stage_text(int stage)
{
    if (stage < 0 || stage >= (int) (sizeof(STAGES) / sizeof(STAGES[0]))) {
        return NULL;
    }
    return STAGES[stage];
}

bool bambu_active(const bambu_status_t *status)
{
    return status->known && (strcmp(status->state, "RUNNING") == 0 || strcmp(status->state, "PAUSE") == 0 ||
                             strcmp(status->state, "PREPARE") == 0 || strcmp(status->state, "SLICING") == 0);
}

static const char *text_for(const bambu_text_t *texts, int text_count, const char *code)
{
    for (int i = 0; i < text_count; i++) {
        if (strcmp(texts[i].code, code) == 0 && texts[i].text[0]) {
            return texts[i].text;
        }
    }
    return NULL;
}

static void add_problem(cJSON *problems, const char *code, const char *severity, const char *text)
{
    cJSON *problem = cJSON_CreateObject();
    cJSON_AddStringToObject(problem, "code", code);
    if (severity) {
        cJSON_AddStringToObject(problem, "severity", severity);
    }
    cJSON_AddStringToObject(problem, "description",
                            text ? text : "popis se nepodařilo stáhnout, řekni pánovi kód chyby");
    cJSON_AddItemToArray(problems, problem);
}

char *bambu_describe(const bambu_status_t *status, const bambu_text_t *texts, int text_count)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", true);
    if (!status->known) {
        cJSON_AddStringToObject(root, "state", "tiskárna zatím neposlala svůj stav");
    } else {
        const char *state = bambu_state_text(status->state);
        cJSON_AddStringToObject(root, "state", state ? state : status->state);
        bool active = bambu_active(status);
        const char *stage = bambu_stage_text(status->stage);
        if (active && stage) {
            cJSON_AddStringToObject(root, "activity", stage);
        }
        if (status->job[0]) {
            cJSON_AddStringToObject(root, active ? "job" : "last_job", status->job);
        }
        if (active) {
            if (status->percent >= 0) {
                cJSON_AddNumberToObject(root, "percent", status->percent);
            }
            if (status->remaining_minutes >= 0) {
                cJSON_AddNumberToObject(root, "remaining_minutes", status->remaining_minutes);
            }
            if (status->total_layers > 0) {
                cJSON_AddNumberToObject(root, "layer", status->layer);
                cJSON_AddNumberToObject(root, "total_layers", status->total_layers);
            }
        }
        if (status->nozzle >= 0) {
            cJSON_AddNumberToObject(root, "nozzle_celsius", status->nozzle);
        }
        if (status->bed >= 0) {
            cJSON_AddNumberToObject(root, "bed_celsius", status->bed);
        }
    }
    cJSON *problems = cJSON_AddArrayToObject(root, "problems");
    char code[BAMBU_CODE_LENGTH];
    if (status->print_error) {
        bambu_error_code(status->print_error, code);
        add_problem(problems, code, NULL, text_for(texts, text_count, code));
    }
    for (int i = 0; i < status->alert_count; i++) {
        bambu_alert_code(&status->alerts[i], code);
        add_problem(problems, code, bambu_alert_severity(&status->alerts[i]), text_for(texts, text_count, code));
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

size_t bambu_summary(const bambu_status_t *status, char *out, size_t capacity)
{
    out[0] = 0;
    if (!bambu_active(status)) {
        return 0;
    }
    int written;
    if (strcmp(status->state, "PAUSE") == 0) {
        written = snprintf(out, capacity, "TISK STOJÍ");
    } else if (status->percent < 0) {
        written = snprintf(out, capacity, "TISK");
    } else if (status->remaining_minutes > 0) {
        written = snprintf(out, capacity, "TISK %d %% · %d:%02d", status->percent, status->remaining_minutes / 60,
                           status->remaining_minutes % 60);
    } else {
        written = snprintf(out, capacity, "TISK %d %%", status->percent);
    }
    return written < 0 ? 0 : (size_t) written < capacity ? (size_t) written : capacity - 1;
}

static bool header_value(const char *packet, size_t length, const char *name, char *out, size_t capacity)
{
    size_t name_length = strlen(name);
    size_t line = 0;
    while (line < length) {
        size_t end = line;
        while (end < length && packet[end] != '\n') {
            end++;
        }
        if (end - line > name_length && packet[line + name_length] == ':') {
            bool match = true;
            for (size_t i = 0; i < name_length && match; i++) {
                match = tolower((unsigned char) packet[line + i]) == tolower((unsigned char) name[i]);
            }
            if (match) {
                size_t from = line + name_length + 1;
                size_t to = end;
                while (from < to && isspace((unsigned char) packet[from])) {
                    from++;
                }
                while (to > from && isspace((unsigned char) packet[to - 1])) {
                    to--;
                }
                if (to - from == 0 || to - from >= capacity) {
                    return false;
                }
                memcpy(out, packet + from, to - from);
                out[to - from] = 0;
                return true;
            }
        }
        line = end + 1;
    }
    return false;
}

static bool contains(const char *packet, size_t length, const char *needle)
{
    size_t needle_length = strlen(needle);
    for (size_t i = 0; i + needle_length <= length; i++) {
        if (memcmp(packet + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

bool bambu_parse_announcement(const char *packet, size_t length, char *address, size_t address_capacity, char *serial,
                              size_t serial_capacity)
{
    char location[96];
    address[0] = 0;
    if (!contains(packet, length, SERVICE) || !header_value(packet, length, "USN", serial, serial_capacity)) {
        return false;
    }
    for (const char *c = serial; *c; c++) {
        if (!isalnum((unsigned char) *c)) {
            return false;
        }
    }
    if (!header_value(packet, length, "Location", location, sizeof(location))) {
        return true;
    }
    const char *host = strstr(location, "://");
    host = host ? host + 3 : location;
    size_t host_length = strcspn(host, ":/");
    if (host_length < 7 || host_length >= address_capacity) {
        return true;
    }
    for (size_t i = 0; i < host_length; i++) {
        if (!isdigit((unsigned char) host[i]) && host[i] != '.') {
            return true;
        }
    }
    memcpy(address, host, host_length);
    address[host_length] = 0;
    return true;
}

bool bambu_parse_lookup(const char *json, size_t length, char *out, size_t capacity)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    bool found = false;
    const cJSON *group;
    cJSON_ArrayForEach(group, data)
    {
        const cJSON *entry;
        cJSON_ArrayForEach(entry, cJSON_GetObjectItemCaseSensitive(group, "en"))
        {
            const cJSON *intro = cJSON_GetObjectItemCaseSensitive(entry, "intro");
            if (!found && cJSON_IsString(intro) && intro->valuestring[0]) {
                size_t take = strlen(intro->valuestring);
                if (take >= capacity) {
                    take = capacity - 1;
                    while (take > 0 && ((unsigned char) intro->valuestring[take] & 0xC0) == 0x80) {
                        take--;
                    }
                }
                memcpy(out, intro->valuestring, take);
                out[take] = 0;
                found = true;
            }
        }
    }
    cJSON_Delete(root);
    return found;
}
