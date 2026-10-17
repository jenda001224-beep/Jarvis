#include "control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ikea.h"

typedef struct {
    char id[56];
    char change[48];
} command_t;

static const char *TAG = "control";
static QueueHandle_t commands;

static void control_task(void *arg)
{
    command_t command;
    for (;;) {
        if (xQueueReceive(commands, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        char args[160];
        snprintf(args, sizeof(args), "{\"ids\":[\"%s\"],%s}", command.id, command.change);
        char *result = ikea_tool_set(args);
        ESP_LOGI(TAG, "%s → %s", args, result ? result : "none");
        cJSON_free(result);
        if (uxQueueMessagesWaiting(commands) == 0) {
            ikea_refresh_soon();
        }
    }
}

void control_start(void)
{
    commands = xQueueCreate(8, sizeof(command_t));
    if (commands) {
        xTaskCreatePinnedToCoreWithCaps(control_task, "control", 12288, NULL, 3, NULL, 0, MALLOC_CAP_SPIRAM);
    }
}

static void send(const char *id, const char *change)
{
    command_t command;
    if (!commands || strlen(id) >= sizeof(command.id) || strpbrk(id, "\"\\")) {
        return;
    }
    strcpy(command.id, id);
    strlcpy(command.change, change, sizeof(command.change));
    xQueueSend(commands, &command, 0);
}

void control_power(const char *id, bool on)
{
    send(id, on ? "\"on\":true" : "\"on\":false");
}

void control_brightness(const char *id, int level)
{
    char change[48];
    snprintf(change, sizeof(change), "\"on\":true,\"brightness\":%d", level);
    send(id, change);
}

void control_white(const char *id, int kelvin)
{
    char change[48];
    snprintf(change, sizeof(change), "\"on\":true,\"kelvin\":%d", kelvin);
    send(id, change);
}

void control_hue(const char *id, int hue)
{
    char change[48];
    snprintf(change, sizeof(change), "\"on\":true,\"hue\":%d,\"saturation\":100", hue);
    send(id, change);
}
