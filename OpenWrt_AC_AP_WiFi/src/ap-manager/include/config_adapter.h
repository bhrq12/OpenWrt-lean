#ifndef CONFIG_ADAPTER_H
#define CONFIG_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/include/common.h"
#include <stdint.h>

typedef enum {
    ADAPTER_RESULT_SUCCESS = 0,
    ADAPTER_RESULT_PARSE_ERROR = 1,
    ADAPTER_RESULT_VALIDATION_ERROR = 2,
    ADAPTER_RESULT_APPLY_ERROR = 3,
    ADAPTER_RESULT_ROLLBACK_ERROR = 4
} adapter_result_t;

typedef struct {
    char config_id[MAX_CONFIG_ID_LEN];
    int version;
    char* raw_config;
} adapter_config_t;

typedef struct {
    adapter_config_t current_config;
    adapter_config_t previous_config;
    int rollback_count;
} config_adapter_t;

int config_adapter_init(config_adapter_t* adapter);

void config_adapter_destroy(config_adapter_t* adapter);

adapter_result_t config_adapter_apply(config_adapter_t* adapter, const char* config_json);

adapter_result_t config_adapter_rollback(config_adapter_t* adapter);

adapter_result_t config_adapter_validate(const char* config_json);

int config_adapter_get_current_version(config_adapter_t* adapter);

const char* config_adapter_get_current_config_id(config_adapter_t* adapter);

#ifdef __cplusplus
}
#endif

#endif