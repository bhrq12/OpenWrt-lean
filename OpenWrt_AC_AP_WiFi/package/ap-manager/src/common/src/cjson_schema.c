#include <cjson/cjson_schema.h>
#include <string.h>

static int validate_type(cJSON* value, const char* type) {
    if (!value || !type) return 0;

    if (strcmp(type, "string") == 0) {
        return cJSON_IsString(value);
    } else if (strcmp(type, "integer") == 0 || strcmp(type, "number") == 0) {
        return cJSON_IsNumber(value);
    } else if (strcmp(type, "boolean") == 0) {
        return cJSON_IsBool(value);
    } else if (strcmp(type, "object") == 0) {
        return cJSON_IsObject(value);
    } else if (strcmp(type, "array") == 0) {
        return cJSON_IsArray(value);
    }
    return 1;
}

static int validate_node(cJSON* node, cJSON* schema) {
    if (!node || !schema) return 0;

    cJSON* type = cJSON_GetObjectItem(schema, "type");
    if (type && cJSON_IsString(type)) {
        if (!validate_type(node, type->valuestring)) return 0;
    }

    if (cJSON_IsObject(node)) {
        cJSON* properties = cJSON_GetObjectItem(schema, "properties");
        cJSON* required = cJSON_GetObjectItem(schema, "required");

        if (required && cJSON_IsArray(required)) {
            cJSON* req_item;
            cJSON_ArrayForEach(req_item, required) {
                if (cJSON_IsString(req_item)) {
                    cJSON* field = cJSON_GetObjectItem(node, req_item->valuestring);
                    if (!field) return 0;
                }
            }
        }

        if (properties && cJSON_IsObject(properties)) {
            cJSON* field;
            cJSON_ArrayForEach(field, node) {
                cJSON* field_schema = cJSON_GetObjectItem(properties, field->string);
                if (field_schema) {
                    if (!validate_node(field, field_schema)) return 0;
                }
            }
        }
    }

    if (cJSON_IsArray(node)) {
        cJSON* items = cJSON_GetObjectItem(schema, "items");
        if (items) {
            cJSON* elem;
            cJSON_ArrayForEach(elem, node) {
                if (!validate_node(elem, items)) return 0;
            }
        }
    }

    return 1;
}

cJSON* cjson_schema_validate(cJSON* config, cJSON* schema) {
    if (!config || !schema) {
        return cJSON_CreateFalse();
    }

    int valid = validate_node(config, schema);
    return valid ? cJSON_CreateTrue() : cJSON_CreateFalse();
}
