#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/include/common.h"
#include <pthread.h>
#include <uthash.h>

typedef enum {
    CONFIG_TYPE_SSID = 1,
    CONFIG_TYPE_RADIO = 2,
    CONFIG_TYPE_GLOBAL = 3
} config_template_type_t;

typedef struct {
    char config_id[MAX_CONFIG_ID_LEN];
    char template_name[MAX_MODEL_LEN];
    int template_type;
    char* config_content;
    char signature[256];
    int version;
    char creator[MAX_MODEL_LEN];
    time_t create_time;
    UT_hash_handle hh;
} config_template_entry_t;

typedef struct {
    config_template_entry_t* templates;
    pthread_rwlock_t rwlock;
} config_manager_t;

int config_manager_init(config_manager_t* cm);

void config_manager_destroy(config_manager_t* cm);

int config_manager_add_template(config_manager_t* cm, const config_template_t* template);

int config_manager_update_template(config_manager_t* cm, const config_template_t* template);

config_template_t* config_manager_get_template(config_manager_t* cm, const char* config_id);

int config_manager_delete_template(config_manager_t* cm, const char* config_id);

int config_manager_get_templates(config_manager_t* cm, config_template_t** templates, int* count);

int config_manager_get_templates_by_type(config_manager_t* cm, int template_type,
                                         config_template_t** templates, int* count);

int config_manager_generate_signature(const char* config_content, char* signature, int signature_len);

int config_manager_verify_signature(const char* config_content, const char* signature);

#ifdef __cplusplus
}
#endif

#endif