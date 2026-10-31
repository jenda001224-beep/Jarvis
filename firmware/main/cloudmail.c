#include "cloudmail.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

char *cloud_lookup_body(const char *const *names, int count)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *records = cJSON_AddArrayToObject(root, "records");
    for (int i = 0; i < count; i++) {
        cJSON *record = cJSON_CreateObject();
        cJSON_AddStringToObject(record, "recordName", names[i]);
        cJSON_AddItemToArray(records, record);
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

static void add_field(cJSON *fields, const char *name, cJSON *value)
{
    cJSON *field = cJSON_CreateObject();
    cJSON_AddItemToObject(field, "value", value);
    cJSON_AddItemToObject(fields, name, field);
}

char *cloud_modify_body(const char *operation, const char *name, const char *type, int64_t seq, const char *data, const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *operations = cJSON_AddArrayToObject(root, "operations");
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "operationType", operation);
    cJSON *record = cJSON_AddObjectToObject(item, "record");
    if (name) {
        cJSON_AddStringToObject(record, "recordName", name);
    }
    cJSON_AddStringToObject(record, "recordType", type);
    cJSON *fields = cJSON_AddObjectToObject(record, "fields");
    add_field(fields, "seq", cJSON_CreateNumber((double) seq));
    if (data) {
        add_field(fields, "data", cJSON_CreateString(data));
    }
    if (text) {
        add_field(fields, "text", cJSON_CreateString(text));
    }
    cJSON_AddItemToArray(operations, item);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void cloud_signature_message(const char *date, const char *body_hash, const char *subpath, char *out, size_t capacity)
{
    snprintf(out, capacity, "%s:%s:%s", date, body_hash, subpath);
}
