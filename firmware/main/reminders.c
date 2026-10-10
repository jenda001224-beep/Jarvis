#include "reminders.h"

#include <string.h>

#define MAGIC 0x4A52454DU

typedef struct {
    uint32_t magic;
    uint32_t next_id;
    reminder_t items[REMINDERS_MAX];
} store_t;

static store_t store = {.magic = MAGIC, .next_id = 1};

static reminder_t *find(uint32_t id)
{
    for (int i = 0; i < REMINDERS_MAX; i++) {
        if (id && store.items[i].id == id) {
            return &store.items[i];
        }
    }
    return NULL;
}

void reminders_load(const void *blob, size_t length)
{
    store_t loaded;
    if (length != sizeof(loaded)) {
        return;
    }
    memcpy(&loaded, blob, sizeof(loaded));
    if (loaded.magic != MAGIC || loaded.next_id == 0) {
        return;
    }
    for (int i = 0; i < REMINDERS_MAX; i++) {
        loaded.items[i].text[REMINDER_TEXT - 1] = 0;
    }
    store = loaded;
}

size_t reminders_blob_size(void)
{
    return sizeof(store);
}

size_t reminders_save(void *blob, size_t capacity)
{
    if (capacity < sizeof(store)) {
        return 0;
    }
    memcpy(blob, &store, sizeof(store));
    return sizeof(store);
}

uint32_t reminders_add(int64_t due, const char *text)
{
    reminder_t *slot = NULL;
    for (int i = 0; i < REMINDERS_MAX && !slot; i++) {
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
    slot->due = due;
    slot->next_try = due;
    size_t length = strlen(text);
    if (length >= REMINDER_TEXT) {
        length = REMINDER_TEXT - 1;
        while (length > 0 && ((unsigned char) text[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(slot->text, text, length);
    slot->text[length] = 0;
    return slot->id;
}

bool reminders_cancel(uint32_t id)
{
    reminder_t *item = find(id);
    if (!item) {
        return false;
    }
    memset(item, 0, sizeof(*item));
    return true;
}

bool reminders_due(int64_t now, reminder_t *out)
{
    const reminder_t *best = NULL;
    for (int i = 0; i < REMINDERS_MAX; i++) {
        const reminder_t *item = &store.items[i];
        if (item->id == 0 || item->attempts >= REMINDER_ATTEMPTS || item->next_try > now) {
            continue;
        }
        if (!best || item->due < best->due) {
            best = item;
        }
    }
    if (best && out) {
        *out = *best;
    }
    return best != NULL;
}

void reminders_defer(uint32_t id, int64_t until)
{
    reminder_t *item = find(id);
    if (item) {
        item->next_try = until;
    }
}

void reminders_attempted(uint32_t id, int64_t now)
{
    reminder_t *item = find(id);
    if (item && item->attempts < REMINDER_ATTEMPTS) {
        item->attempts++;
        item->next_try = now + REMINDER_RETRY_SECONDS;
    }
}

static int collect(bool unheard, reminder_t *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < REMINDERS_MAX; i++) {
        const reminder_t *item = &store.items[i];
        if (item->id == 0 || (item->attempts > 0) != unheard) {
            continue;
        }
        if (out && count < capacity) {
            int at = count;
            while (at > 0 && out[at - 1].due > item->due) {
                out[at] = out[at - 1];
                at--;
            }
            out[at] = *item;
        }
        count++;
    }
    return out && count > capacity ? capacity : count;
}

int reminders_unheard(reminder_t *out, int capacity)
{
    return collect(true, out, capacity);
}

int reminders_pending(reminder_t *out, int capacity)
{
    return collect(false, out, capacity);
}

int reminders_drop_unheard(void)
{
    int dropped = 0;
    for (int i = 0; i < REMINDERS_MAX; i++) {
        if (store.items[i].id && store.items[i].attempts > 0) {
            memset(&store.items[i], 0, sizeof(store.items[i]));
            dropped++;
        }
    }
    return dropped;
}
