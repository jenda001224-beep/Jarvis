#include "notices.h"

#include <stdio.h>
#include <string.h>

#define REPEAT_SECONDS 60

static notice_t items[NOTICES_MAX];
static int count;
static uint32_t next_id = 1;

static void copy_utf8(char *out, size_t capacity, const char *text)
{
    size_t length = strlen(text);
    if (length >= capacity) {
        length = capacity - 1;
        while (length > 0 && ((unsigned char) text[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(out, text, length);
    out[length] = 0;
}

uint32_t notices_add(int64_t when, const char *source, const char *text, bool speak)
{
    if (!text || !text[0]) {
        return 0;
    }
    char clipped[NOTICE_TEXT];
    copy_utf8(clipped, sizeof(clipped), text);
    for (char *c = clipped; *c; c++) {
        if ((unsigned char) *c < 0x20) {
            *c = ' ';
        }
    }
    for (int i = 0; i < count; i++) {
        if (when - items[i].when < REPEAT_SECONDS && strcmp(items[i].text, clipped) == 0) {
            return items[i].id;
        }
    }
    if (count == NOTICES_MAX) {
        memmove(&items[0], &items[1], sizeof(items[0]) * (NOTICES_MAX - 1));
        count--;
    }
    notice_t *item = &items[count++];
    memset(item, 0, sizeof(*item));
    item->id = next_id++;
    item->when = when;
    item->speak = speak;
    copy_utf8(item->source, sizeof(item->source), source ? source : "");
    strcpy(item->text, clipped);
    return item->id;
}

int notices_list(notice_t *out, int capacity)
{
    int shown = 0;
    for (int i = count - 1; i >= 0 && shown < capacity; i--) {
        out[shown++] = items[i];
    }
    return shown;
}

int notices_unread(void)
{
    int unread = 0;
    for (int i = 0; i < count; i++) {
        unread += !items[i].read;
    }
    return unread;
}

size_t notices_join(char *out, size_t capacity, bool only_spoken, bool mark)
{
    size_t used = 0;
    if (capacity == 0) {
        return 0;
    }
    out[0] = 0;
    for (int i = 0; i < count; i++) {
        if (items[i].read || (only_spoken && !items[i].speak)) {
            continue;
        }
        size_t size = strlen(items[i].text);
        size_t separator = used ? 2 : 0;
        if (used + separator + size + 1 > capacity) {
            break;
        }
        if (separator) {
            memcpy(out + used, "; ", 2);
            used += 2;
        }
        memcpy(out + used, items[i].text, size + 1);
        used += size;
        if (mark) {
            items[i].read = true;
        }
    }
    return used;
}

void notices_mark_all(void)
{
    for (int i = 0; i < count; i++) {
        items[i].read = true;
    }
}

void notices_clear(void)
{
    count = 0;
}
