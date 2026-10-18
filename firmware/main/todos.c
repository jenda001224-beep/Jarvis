#include "todos.h"

#include <ctype.h>
#include <string.h>

#define MAGIC 0x4A544F44U

typedef struct {
    uint32_t magic;
    uint32_t next_id;
    todo_t items[TODOS_MAX];
} store_t;

static store_t store = {.magic = MAGIC, .next_id = 1};

static todo_t *find(uint32_t id)
{
    for (int i = 0; i < TODOS_MAX; i++) {
        if (id && store.items[i].id == id) {
            return &store.items[i];
        }
    }
    return NULL;
}

static void copy_text(char *out, const char *text)
{
    while (*text == ' ') {
        text++;
    }
    size_t length = strlen(text);
    if (length >= TODO_TEXT) {
        length = TODO_TEXT - 1;
        while (length > 0 && ((unsigned char) text[length] & 0xC0) == 0x80) {
            length--;
        }
    }
    while (length > 0 && text[length - 1] == ' ') {
        length--;
    }
    memcpy(out, text, length);
    out[length] = 0;
}

void todos_load(const void *blob, size_t length)
{
    store_t loaded;
    if (length != sizeof(loaded)) {
        return;
    }
    memcpy(&loaded, blob, sizeof(loaded));
    if (loaded.magic != MAGIC || loaded.next_id == 0) {
        return;
    }
    for (int i = 0; i < TODOS_MAX; i++) {
        loaded.items[i].text[TODO_TEXT - 1] = 0;
    }
    store = loaded;
}

size_t todos_blob_size(void)
{
    return sizeof(store);
}

size_t todos_save(void *blob, size_t capacity)
{
    if (capacity < sizeof(store)) {
        return 0;
    }
    memcpy(blob, &store, sizeof(store));
    return sizeof(store);
}

uint32_t todos_add(const char *text, int64_t now)
{
    char clean[TODO_TEXT];
    copy_text(clean, text);
    if (!clean[0]) {
        return 0;
    }
    todo_t *slot = NULL;
    for (int i = 0; i < TODOS_MAX && !slot; i++) {
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
    slot->created = now;
    strcpy(slot->text, clean);
    return slot->id;
}

bool todos_set_done(uint32_t id, bool done)
{
    todo_t *todo = find(id);
    if (!todo) {
        return false;
    }
    todo->done = done;
    return true;
}

bool todos_rename(uint32_t id, const char *text)
{
    todo_t *todo = find(id);
    char clean[TODO_TEXT];
    copy_text(clean, text);
    if (!todo || !clean[0]) {
        return false;
    }
    strcpy(todo->text, clean);
    return true;
}

bool todos_remove(uint32_t id)
{
    todo_t *todo = find(id);
    if (!todo) {
        return false;
    }
    memset(todo, 0, sizeof(*todo));
    return true;
}

int todos_clear_done(void)
{
    int removed = 0;
    for (int i = 0; i < TODOS_MAX; i++) {
        if (store.items[i].id && store.items[i].done) {
            memset(&store.items[i], 0, sizeof(store.items[i]));
            removed++;
        }
    }
    return removed;
}

int todos_list(todo_t *out, int capacity)
{
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < TODOS_MAX; i++) {
            const todo_t *todo = &store.items[i];
            if (todo->id && todo->done == (pass == 1) && count < capacity) {
                out[count++] = *todo;
            }
        }
    }
    for (int i = 1; i < count; i++) {
        todo_t current = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].done == current.done && out[j].created > current.created) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = current;
    }
    return count;
}

int todos_open_count(void)
{
    int count = 0;
    for (int i = 0; i < TODOS_MAX; i++) {
        count += store.items[i].id && !store.items[i].done;
    }
    return count;
}

static bool same_text(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

uint32_t todos_find(const char *text)
{
    char clean[TODO_TEXT];
    copy_text(clean, text);
    for (int i = 0; i < TODOS_MAX; i++) {
        if (store.items[i].id && same_text(store.items[i].text, clean)) {
            return store.items[i].id;
        }
    }
    return 0;
}
