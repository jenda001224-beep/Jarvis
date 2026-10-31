#pragma once

#include <stdbool.h>
#include <stddef.h>

char *gemini_text_request(const char *system, const char *user, int max_tokens);
char *gemini_text_answer(const char *response, size_t length);

#define GEMINI_CALLS 8

typedef struct {
    char *name;
    char *args;
} gemini_call_t;

typedef struct {
    char *text;
    char *content;
    int call_count;
    gemini_call_t calls[GEMINI_CALLS];
} gemini_turn_t;

char *gemini_tools(const char *functions_json);
char *gemini_chat_request(const char *system, const char *contents_json, const char *tools_json, int max_tokens);
bool gemini_parse_turn(const char *response, size_t length, gemini_turn_t *turn);
void gemini_turn_free(gemini_turn_t *turn);
char *gemini_append_turn(const char *contents_json, const char *role, const char *text);
char *gemini_append_content(const char *contents_json, const char *content_json);
char *gemini_append_responses(const char *contents_json, const gemini_call_t *calls, char *const *results, int count);
