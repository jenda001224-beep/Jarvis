#pragma once

#include <stddef.h>

char *tavily_request(const char *query);
char *tavily_summarize(const char *json, size_t length);
