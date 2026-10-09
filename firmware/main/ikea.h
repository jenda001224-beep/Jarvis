#pragma once

#include <stdbool.h>

typedef enum {
    IKEA_SEARCHING,
    IKEA_NOT_FOUND,
    IKEA_UNPAIRED,
    IKEA_PAIRING,
    IKEA_READY,
} ikea_state_t;

void ikea_start(void);
ikea_state_t ikea_state(void);
int ikea_device_count(void);
void ikea_request_pairing(void);
char *ikea_describe_devices(void);
char *ikea_tool_get(void);
char *ikea_tool_set(const char *args_json);
