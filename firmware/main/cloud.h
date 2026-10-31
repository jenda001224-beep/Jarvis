#pragma once

#include <stdbool.h>

#include "api.h"

typedef void (*cloud_tokens_t)(char out[API_TOKENS][API_TOKEN_LENGTH + 1]);

void cloud_start(cloud_tokens_t tokens);
bool cloud_configured(void);
void cloud_push(const char *text);
void cloud_set_idle(bool idle);
const char *cloud_problem(void);
