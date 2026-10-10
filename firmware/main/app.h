#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_EVENT_WAKE,
    APP_EVENT_TOUCH,
    APP_EVENT_NETWORK,
    APP_EVENT_SESSION_READY,
    APP_EVENT_SESSION_CLOSED,
    APP_EVENT_MODEL_AUDIO,
    APP_EVENT_TURN_COMPLETE,
    APP_EVENT_INTERRUPTED,
    APP_EVENT_HEARD,
    APP_EVENT_PAIR_REQUEST,
    APP_EVENT_HUB_CHANGED,
    APP_EVENT_HUB_PAIRED,
} app_event_t;

void app_post(app_event_t event);
