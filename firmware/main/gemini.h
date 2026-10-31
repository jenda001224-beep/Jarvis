#pragma once

#include <stddef.h>

char *gemini_text_request(const char *system, const char *user, int max_tokens);
char *gemini_text_answer(const char *response, size_t length);
