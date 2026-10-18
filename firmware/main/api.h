#pragma once

#include <stdbool.h>
#include <stddef.h>

#define API_TOKENS 4
#define API_TOKEN_LENGTH 32

typedef enum {
    API_GET,
    API_POST,
    API_PUT,
    API_PATCH,
    API_DELETE,
} api_method_t;

typedef struct {
    api_method_t method;
    char path[64];
    char *body;
    int status;
    char *reply;
    bool authorized;
    char token[API_TOKEN_LENGTH + 1];
} api_call_t;

typedef void (*api_handler_t)(api_call_t *call);

void api_start(const char *version);
void api_set_tokens(const char tokens[API_TOKENS][API_TOKEN_LENGTH + 1]);
void api_serve(api_handler_t handler);
