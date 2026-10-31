#include "crash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_system.h"
#include "settings.h"
#include "watch.h"

#define NVS_CRASH "last_crash"
#define TEXT_SIZE 320

static const char *TAG = "crash";
static char text[TEXT_SIZE];

static void describe(const esp_core_dump_summary_t *summary)
{
    int used = snprintf(text, sizeof(text), "%s %s, task %.16s, pc 0x%08lx, cause %lu, addr 0x%08lx, bt", esp_app_get_description()->version,
                        esp_reset_reason() == ESP_RST_TASK_WDT || esp_reset_reason() == ESP_RST_INT_WDT ? "watchdog" : "crash", summary->exc_task, (unsigned long) summary->exc_pc,
                        (unsigned long) summary->ex_info.exc_cause, (unsigned long) summary->ex_info.exc_vaddr);
    for (uint32_t i = 0; i < summary->exc_bt_info.depth && used > 0 && (size_t) used + 12 < sizeof(text); i++) {
        used += snprintf(text + used, sizeof(text) - (size_t) used, " 0x%08lx", (unsigned long) summary->exc_bt_info.bt[i]);
    }
}

bool crash_check(void)
{
    size_t stored = settings_read_blob(NVS_CRASH, text, sizeof(text) - 1);
    text[stored] = 0;
    esp_reset_reason_t reason = esp_reset_reason();
    bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
    if (esp_core_dump_image_check() != ESP_OK) {
        if (crashed) {
            snprintf(text, sizeof(text), "%s, reset reason %d, no dump", esp_app_get_description()->version, (int) reason);
            settings_write_blob(NVS_CRASH, text, strlen(text));
        }
        return crashed;
    }
    esp_core_dump_summary_t *summary = calloc(1, sizeof(esp_core_dump_summary_t));
    if (summary && esp_core_dump_get_summary(summary) == ESP_OK) {
        describe(summary);
        settings_write_blob(NVS_CRASH, text, strlen(text));
        ESP_LOGE(TAG, "%s", text);
    }
    free(summary);
    esp_core_dump_image_erase();
    return true;
}

const char *crash_text(void)
{
    return text;
}
