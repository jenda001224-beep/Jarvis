#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BAMBU_MAX_ALERTS 4
#define BAMBU_CODE_LENGTH 17
#define BAMBU_TEXT_LENGTH 360

typedef struct {
    uint32_t attr;
    uint32_t code;
} bambu_alert_t;

typedef struct {
    bool known;
    char state[12];
    char job[64];
    int percent;
    int remaining_minutes;
    int layer;
    int total_layers;
    int stage;
    int nozzle;
    int bed;
    uint32_t print_error;
    int alert_count;
    bambu_alert_t alerts[BAMBU_MAX_ALERTS];
} bambu_status_t;

typedef struct {
    char code[BAMBU_CODE_LENGTH];
    char text[BAMBU_TEXT_LENGTH];
} bambu_text_t;

void bambu_status_reset(bambu_status_t *status);
bool bambu_merge_report(bambu_status_t *status, const char *json, size_t length);
void bambu_alert_code(const bambu_alert_t *alert, char out[BAMBU_CODE_LENGTH]);
void bambu_error_code(uint32_t print_error, char out[BAMBU_CODE_LENGTH]);
const char *bambu_alert_severity(const bambu_alert_t *alert);
const char *bambu_state_text(const char *state);
const char *bambu_stage_text(int stage);
bool bambu_active(const bambu_status_t *status);
char *bambu_describe(const bambu_status_t *status, const bambu_text_t *texts, int text_count);
size_t bambu_summary(const bambu_status_t *status, char *out, size_t capacity);
bool bambu_parse_announcement(const char *packet, size_t length, char *address, size_t address_capacity, char *serial,
                              size_t serial_capacity);
bool bambu_parse_lookup(const char *json, size_t length, char *out, size_t capacity);
