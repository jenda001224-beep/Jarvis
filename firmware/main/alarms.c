#include "alarms.h"

#include <string.h>

#define MAGIC 0x4A414C4DU
#define DAYS_AHEAD 8
#define SECONDS_PER_DAY 86400

typedef struct {
    uint32_t magic;
    uint32_t next_id;
    alarm_t items[ALARMS_MAX];
} store_t;

static store_t store = {.magic = MAGIC, .next_id = 1};

static alarm_t *find(uint32_t id)
{
    for (int i = 0; i < ALARMS_MAX; i++) {
        if (id && store.items[i].id == id) {
            return &store.items[i];
        }
    }
    return NULL;
}

static void copy_label(char *out, const char *label)
{
    size_t length = label ? strlen(label) : 0;
    if (length >= ALARM_LABEL) {
        length = ALARM_LABEL - 1;
        while (length > 0 && ((unsigned char) label[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    if (length) {
        memcpy(out, label, length);
    }
    out[length] = 0;
}

static bool valid(int hour, int minute, uint8_t days)
{
    return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && days <= ALARM_EVERY_DAY;
}

uint8_t alarms_day_bit(int weekday)
{
    return (uint8_t) (1u << ((weekday + 6) % 7));
}

static int32_t day_number(const struct tm *local)
{
    return (int32_t) ((local->tm_year + 1900) * 400 + local->tm_yday);
}

void alarms_load(const void *blob, size_t length)
{
    store_t loaded;
    if (length != sizeof(loaded)) {
        return;
    }
    memcpy(&loaded, blob, sizeof(loaded));
    if (loaded.magic != MAGIC || loaded.next_id == 0) {
        return;
    }
    for (int i = 0; i < ALARMS_MAX; i++) {
        loaded.items[i].label[ALARM_LABEL - 1] = 0;
    }
    store = loaded;
}

size_t alarms_blob_size(void)
{
    return sizeof(store);
}

size_t alarms_save(void *blob, size_t capacity)
{
    if (capacity < sizeof(store)) {
        return 0;
    }
    memcpy(blob, &store, sizeof(store));
    return sizeof(store);
}

uint32_t alarms_add(int hour, int minute, uint8_t days, const char *label)
{
    if (!valid(hour, minute, days)) {
        return 0;
    }
    alarm_t *slot = NULL;
    for (int i = 0; i < ALARMS_MAX && !slot; i++) {
        if (store.items[i].id == 0) {
            slot = &store.items[i];
        }
    }
    if (!slot) {
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    slot->id = store.next_id++;
    if (store.next_id == 0) {
        store.next_id = 1;
    }
    slot->hour = (uint8_t) hour;
    slot->minute = (uint8_t) minute;
    slot->days = days;
    slot->enabled = true;
    slot->fired_day = -1;
    copy_label(slot->label, label);
    return slot->id;
}

bool alarms_update(uint32_t id, int hour, int minute, uint8_t days, const char *label, bool enabled)
{
    alarm_t *alarm = find(id);
    if (!alarm || !valid(hour, minute, days)) {
        return false;
    }
    if (alarm->hour != hour || alarm->minute != minute || (enabled && !alarm->enabled)) {
        alarm->fired_day = -1;
    }
    alarm->hour = (uint8_t) hour;
    alarm->minute = (uint8_t) minute;
    alarm->days = days;
    alarm->enabled = enabled;
    copy_label(alarm->label, label);
    return true;
}

bool alarms_remove(uint32_t id)
{
    alarm_t *alarm = find(id);
    if (!alarm) {
        return false;
    }
    memset(alarm, 0, sizeof(*alarm));
    return true;
}

bool alarms_get(uint32_t id, alarm_t *out)
{
    alarm_t *alarm = find(id);
    if (!alarm) {
        return false;
    }
    *out = *alarm;
    return true;
}

int alarms_list(alarm_t *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < ALARMS_MAX && count < capacity; i++) {
        if (store.items[i].id) {
            out[count++] = store.items[i];
        }
    }
    for (int i = 1; i < count; i++) {
        alarm_t current = out[i];
        int key = current.hour * 60 + current.minute;
        int j = i - 1;
        while (j >= 0 && out[j].hour * 60 + out[j].minute > key) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = current;
    }
    return count;
}

static bool rings_on(const alarm_t *alarm, const struct tm *local)
{
    return alarm->enabled && (alarm->days == 0 || (alarm->days & alarms_day_bit(local->tm_wday)));
}

bool alarms_due(time_t now, alarm_t *out)
{
    struct tm local;
    localtime_r(&now, &local);
    int32_t today = day_number(&local);
    for (int i = 0; i < ALARMS_MAX; i++) {
        alarm_t *alarm = &store.items[i];
        if (!alarm->id || !rings_on(alarm, &local) || alarm->fired_day == today) {
            continue;
        }
        if (alarm->hour == local.tm_hour && alarm->minute == local.tm_min) {
            alarm->fired_day = today;
            if (alarm->days == 0) {
                alarm->enabled = false;
            }
            *out = *alarm;
            return true;
        }
    }
    return false;
}

bool alarms_next(time_t now, alarm_t *out, time_t *when)
{
    bool found = false;
    struct tm today;
    localtime_r(&now, &today);
    for (int ahead = 0; ahead < DAYS_AHEAD; ahead++) {
        struct tm day = today;
        day.tm_mday += ahead;
        day.tm_hour = 12;
        day.tm_min = 0;
        day.tm_sec = 0;
        day.tm_isdst = -1;
        time_t noon = mktime(&day);
        struct tm local;
        localtime_r(&noon, &local);
        for (int i = 0; i < ALARMS_MAX; i++) {
            const alarm_t *alarm = &store.items[i];
            if (!alarm->id || !rings_on(alarm, &local)) {
                continue;
            }
            struct tm ring = local;
            ring.tm_hour = alarm->hour;
            ring.tm_min = alarm->minute;
            ring.tm_sec = 0;
            ring.tm_isdst = -1;
            time_t at = mktime(&ring);
            if (at <= now || (ahead == 0 && alarm->fired_day == day_number(&local))) {
                continue;
            }
            if (!found || at < *when) {
                found = true;
                *when = at;
                *out = *alarm;
            }
        }
        if (found) {
            return true;
        }
    }
    return false;
}
