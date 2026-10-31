#pragma once

#include <stdbool.h>

typedef struct {
    char id[56];
    char name[48];
    char room[32];
    char hub_name[48];
    char hub_room[32];
    bool hidden;
    bool welcome;
    bool light;
    bool on;
    bool reachable;
    bool color;
    bool white;
    int level;
} ikea_device_t;

#define IKEA_ALIASES 24

typedef struct {
    char id[56];
    char name[48];
    char room[32];
    bool named;
    bool roomed;
    bool hidden;
    bool welcome;
} ikea_alias_t;

typedef enum {
    IKEA_SEARCHING,
    IKEA_NOT_FOUND,
    IKEA_UNPAIRED,
    IKEA_PAIRING,
    IKEA_READY,
} ikea_state_t;

void ikea_start(const char *stored_token);
const char *ikea_token(void);
ikea_state_t ikea_state(void);
int ikea_device_count(void);
void ikea_request_pairing(void);
char *ikea_describe_devices(void);
char *ikea_tool_get(void);
char *ikea_tool_set(const char *args_json);
int ikea_devices(ikea_device_t *out, int capacity);
void ikea_refresh_soon(void);
void ikea_set_aliases(const ikea_alias_t *list, int count);
