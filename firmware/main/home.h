#pragma once

#include <stdbool.h>

#include "lvgl.h"

#define HOME_DEVICES 24
#define HOME_ORB_X 240
#define HOME_ORB_Y 440
#define HOME_ORB_RADIUS 22

typedef enum {
    HOME_LIGHT,
    HOME_STRIP,
    HOME_OUTLET,
} home_kind_t;

typedef enum {
    HOME_HUB_HIDDEN,
    HOME_HUB_OFFER,
    HOME_HUB_PAIRING,
    HOME_HUB_READY,
} home_hub_t;

typedef struct {
    char id[56];
    char name[40];
    char room[32];
    home_kind_t kind;
    bool on;
    bool reachable;
    bool color;
    bool white;
    int level;
} home_device_t;

#define HOME_SLEEP_CHOICES 5

typedef struct {
    int volume;
    int brightness;
    int sleep;
    int dark;
    int orientation;
    int sensitivity;
    int night;
    int night_from;
    int night_to;
} home_prefs_t;

typedef struct {
    char time[8];
    char date[40];
    bool weather;
    int celsius;
    int sky;
    char sky_text[32];
    char printer[40];
    char event[72];
    int reminders;
    int todos;
    char alarm[24];
    home_hub_t hub;
    int device_count;
    home_device_t devices[HOME_DEVICES];
    home_prefs_t prefs;
    char info[96];
} home_data_t;

typedef struct {
    void (*power)(const char *id, bool on);
    void (*brightness)(const char *id, int level);
    void (*white)(const char *id, int kelvin);
    void (*hue)(const char *id, int hue);
    void (*talk)(void);
    void (*lamp)(void);
    void (*prefs)(const home_prefs_t *prefs);
    void (*pair)(void);
} home_actions_t;

typedef struct {
    const lv_font_t *clock;
    const lv_font_t *temperature;
    const lv_font_t *heading;
    const lv_font_t *body;
    const lv_font_t *small;
    const lv_font_t *caption;
    const lv_font_t *icon;
    const lv_font_t *icon_large;
} home_fonts_t;

lv_obj_t *home_create(lv_obj_t *parent, const home_fonts_t *fonts, const home_actions_t *actions);
void home_update(const home_data_t *data);
extern const int HOME_SLEEP_SECONDS[HOME_SLEEP_CHOICES];

void home_close_sheet(void);
bool home_sheet_open(void);
