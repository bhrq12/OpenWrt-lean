#ifndef CJSON_SCHEMA_H
#define CJSON_SCHEMA_H

#include <cjson/cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

cJSON* cjson_schema_validate(cJSON* config, cJSON* schema);

#ifdef __cplusplus
}
#endif

#endif
