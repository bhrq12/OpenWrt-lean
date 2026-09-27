#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONFIG_KEY_LEN 128
#define MAX_CONFIG_VALUE_LEN 512

typedef struct config_item {
    char key[MAX_CONFIG_KEY_LEN];
    char value[MAX_CONFIG_VALUE_LEN];
    struct config_item* next;
} config_item_t;

typedef struct {
    config_item_t* items;
    char filename[256];
} config_t;

config_t* config_create(const char* filename);

void config_destroy(config_t* config);

int config_load(config_t* config);

int config_save(config_t* config);

const char* config_get(config_t* config, const char* key, const char* default_value);

int config_get_int(config_t* config, const char* key, int default_value);

int config_set(config_t* config, const char* key, const char* value);

int config_set_int(config_t* config, const char* key, int value);

#ifdef __cplusplus
}
#endif

#endif