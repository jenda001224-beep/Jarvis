#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bambu.h"

void printer_start(void);
bool printer_configured(void);
char *printer_tool_status(void);
bool printer_status(bambu_status_t *out);
size_t printer_summary(char *out, size_t capacity);
size_t printer_brief(char *out, size_t capacity);
