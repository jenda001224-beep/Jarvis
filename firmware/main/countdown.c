#include "countdown.h"

#include "textfold.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static countdown_t items[COUNTDOWN_MAX];
static uint32_t next_id = 1;

static void copy_label(char *out, const char *label)
{
    size_t length = label ? strlen(label) : 0;
    if (length >= COUNTDOWN_LABEL) {
        length = COUNTDOWN_LABEL - 1;
        while (length > 0 && ((unsigned char) label[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    if (length) {
        memcpy(out, label, length);
    }
    out[length] = 0;
}

uint32_t countdown_add(int64_t now_ms, int seconds, const char *label)
{
    if (seconds <= 0 || seconds > COUNTDOWN_LONGEST_SECONDS) {
        return 0;
    }
    for (int i = 0; i < COUNTDOWN_MAX; i++) {
        if (items[i].id) {
            continue;
        }
        items[i].id = next_id++;
        if (next_id == 0) {
            next_id = 1;
        }
        items[i].ends_ms = now_ms + (int64_t) seconds * 1000;
        items[i].seconds = seconds;
        copy_label(items[i].label, label);
        return items[i].id;
    }
    return 0;
}

bool countdown_cancel(uint32_t id)
{
    for (int i = 0; id && i < COUNTDOWN_MAX; i++) {
        if (items[i].id == id) {
            memset(&items[i], 0, sizeof(items[i]));
            return true;
        }
    }
    return false;
}

static bool same_label(const char *a, const char *b)
{
    char first[COUNTDOWN_LABEL];
    char second[COUNTDOWN_LABEL];
    text_fold(a, first, sizeof(first));
    text_fold(b, second, sizeof(second));
    return strcmp(first, second) == 0;
}

uint32_t countdown_find(const char *label)
{
    for (int i = 0; label && i < COUNTDOWN_MAX; i++) {
        if (items[i].id && same_label(items[i].label, label)) {
            return items[i].id;
        }
    }
    return 0;
}

int countdown_clear(void)
{
    int removed = 0;
    for (int i = 0; i < COUNTDOWN_MAX; i++) {
        removed += items[i].id != 0;
    }
    memset(items, 0, sizeof(items));
    return removed;
}

int countdown_list(countdown_t *out, int capacity)
{
    int count = 0;
    for (int i = 0; i < COUNTDOWN_MAX; i++) {
        if (!items[i].id) {
            continue;
        }
        countdown_t current = items[i];
        int place = count < capacity ? count : capacity - 1;
        if (place < 0) {
            break;
        }
        if (count >= capacity && out[place].ends_ms <= current.ends_ms) {
            continue;
        }
        while (place > 0 && out[place - 1].ends_ms > current.ends_ms) {
            out[place] = out[place - 1];
            place--;
        }
        out[place] = current;
        if (count < capacity) {
            count++;
        }
    }
    return count;
}

bool countdown_due(int64_t now_ms, countdown_t *out)
{
    countdown_t *earliest = NULL;
    for (int i = 0; i < COUNTDOWN_MAX; i++) {
        if (items[i].id && items[i].ends_ms <= now_ms && (!earliest || items[i].ends_ms < earliest->ends_ms)) {
            earliest = &items[i];
        }
    }
    if (!earliest) {
        return false;
    }
    *out = *earliest;
    memset(earliest, 0, sizeof(*earliest));
    return true;
}

void countdown_text(int64_t remaining_ms, char *out, size_t capacity)
{
    int total = remaining_ms <= 0 ? 0 : (int) ((remaining_ms + 999) / 1000);
    int hours = total / 3600;
    int minutes = (total % 3600) / 60;
    int seconds = total % 60;
    if (hours) {
        snprintf(out, capacity, "%d:%02d:%02d", hours, minutes, seconds);
    } else {
        snprintf(out, capacity, "%d:%02d", minutes, seconds);
    }
}
