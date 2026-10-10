#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define AGENDA_TITLE 96
#define AGENDA_PLACE 160

typedef struct {
    time_t start;
    time_t end;
    bool all_day;
    char title[AGENDA_TITLE];
    char place[AGENDA_PLACE];
} agenda_event_t;

typedef struct agenda_parser agenda_parser_t;

size_t agenda_parser_size(void);
void agenda_parser_begin(agenda_parser_t *parser, time_t window_start, time_t window_end);
void agenda_parser_feed(agenda_parser_t *parser, const char *data, size_t length);
void agenda_parser_source_end(agenda_parser_t *parser);
int agenda_parser_finish(agenda_parser_t *parser, agenda_event_t *out, int capacity);
bool agenda_overlaps(const agenda_event_t *event, time_t from, time_t to);
char *agenda_describe(const agenda_event_t *events, int count, time_t from, time_t to);
size_t agenda_summary(const agenda_event_t *events, int count, time_t from, time_t to, char *out, size_t capacity);
