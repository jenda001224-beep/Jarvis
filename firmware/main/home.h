#pragma once

#include <stdbool.h>
#include <stdint.h>

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
#define HOME_TIMERS 4
#define HOME_TODOS 14
#define HOME_AGENDA 8
#define HOME_NOTICES 6
#define HOME_NOISES 3

typedef struct {
    uint32_t id;
    int seconds;
    int remaining;
    char label[40];
    char left[12];
} home_timer_t;

typedef struct {
    uint32_t id;
    bool done;
    char text[80];
} home_todo_t;

typedef struct {
    char when[16];
    char title[72];
} home_event_t;

typedef struct {
    char when[8];
    char text[200];
} home_notice_t;

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
    int announce;
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
    char quiet[24];
    int timer_count;
    home_timer_t timers[HOME_TIMERS];
    int todo_count;
    home_todo_t todo_items[HOME_TODOS];
    int agenda_count;
    home_event_t agenda[HOME_AGENDA];
    char playing[64];
    int unread;
    int notice_count;
    home_notice_t notices[HOME_NOTICES];
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
    void (*todo)(uint32_t id, bool done);
    void (*timer_add)(int seconds);
    void (*timer_cancel)(uint32_t id);
    void (*notices_seen)(void);
    void (*media_station)(int index);
    void (*media_noise)(int kind);
    void (*media_stop)(void);
    const char *(*station_name)(int index);
    int station_count;
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
