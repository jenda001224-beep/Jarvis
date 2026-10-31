#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JOURNAL_SECTOR 4096
#define JOURNAL_NOTES_MAX 4000
#define JOURNAL_LINE_MAX 400
#define JOURNAL_NOTES_SECTORS 2

typedef struct {
    bool (*read)(void *context, size_t offset, void *out, size_t length);
    bool (*write)(void *context, size_t offset, const void *data, size_t length);
    bool (*erase)(void *context, size_t offset, size_t length);
    void *context;
    size_t size;
} journal_flash_t;

typedef struct {
    int64_t when;
    char who;
    const char *text;
} journal_line_t;

typedef bool (*journal_visit_t)(void *context, const journal_line_t *line, size_t offset, size_t length);

typedef struct {
    journal_flash_t flash;
    size_t sectors;
    size_t head;
    size_t used;
    uint32_t seq;
    uint32_t notes_seq;
    int notes_slot;
    uint8_t buffer[JOURNAL_SECTOR];
} journal_t;

bool journal_open(journal_t *journal, const journal_flash_t *flash);
bool journal_append(journal_t *journal, int64_t when, char who, const char *text);
void journal_each(journal_t *journal, journal_visit_t visit, void *context);
size_t journal_since(journal_t *journal, int64_t after, char *out, size_t capacity);
size_t journal_recent(journal_t *journal, size_t limit, char *out, size_t capacity);
int journal_search(journal_t *journal, const char *query, journal_visit_t visit, void *context, int limit);
int journal_forget(journal_t *journal, const char *query);
bool journal_wipe(journal_t *journal);
int64_t journal_newest(journal_t *journal, char who);
int journal_count(journal_t *journal);

size_t journal_notes_read(journal_t *journal, char *out, size_t capacity);
bool journal_notes_write(journal_t *journal, const char *text);
int journal_notes_forget(journal_t *journal, const char *query);
bool journal_notes_add(journal_t *journal, const char *line);
