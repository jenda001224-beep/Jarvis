#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TODOS_MAX 30
#define TODO_TEXT 80

typedef struct {
    uint32_t id;
    bool done;
    int64_t created;
    char text[TODO_TEXT];
} todo_t;

void todos_load(const void *blob, size_t length);
size_t todos_save(void *blob, size_t capacity);
size_t todos_blob_size(void);
uint32_t todos_add(const char *text, int64_t now);
bool todos_set_done(uint32_t id, bool done);
bool todos_rename(uint32_t id, const char *text);
bool todos_remove(uint32_t id);
int todos_clear_done(void);
int todos_list(todo_t *out, int capacity);
int todos_open_count(void);
uint32_t todos_find(const char *text);
