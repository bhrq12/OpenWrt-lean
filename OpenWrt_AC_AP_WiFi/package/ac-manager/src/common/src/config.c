#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

config_t* config_create(const char* filename) {
    config_t* config = (config_t*)malloc(sizeof(config_t));
    if (!config) {
        return NULL;
    }
    
    memset(config, 0, sizeof(config_t));
    
    if (filename) {
        strncpy(config->filename, filename, sizeof(config->filename) - 1);
    }
    
    config->items = NULL;
    return config;
}

void config_destroy(config_t* config) {
    if (!config) {
        return;
    }
    
    config_item_t* item = config->items;
    while (item) {
        config_item_t* next = item->next;
        free(item);
        item = next;
    }
    
    free(config);
}

static char* trim(char* str) {
    if (!str) return NULL;
    
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    return str;
}

int config_load(config_t* config) {
    if (!config || !config->filename[0]) {
        return -1;
    }
    
    FILE* file = fopen(config->filename, "r");
    if (!file) {
        return 0;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char* comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }
        
        char* equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        
        *equals = '\0';
        char* key = trim(line);
        char* value = trim(equals + 1);
        
        if (!key || !value || strlen(key) == 0) {
            continue;
        }
        
        config_item_t* item = (config_item_t*)malloc(sizeof(config_item_t));
        if (!item) {
            continue;
        }
        
        strncpy(item->key, key, sizeof(item->key) - 1);
        strncpy(item->value, value, sizeof(item->value) - 1);
        item->next = config->items;
        config->items = item;
    }
    
    fclose(file);
    return 0;
}

int config_save(config_t* config) {
    if (!config || !config->filename[0]) {
        return -1;
    }
    
    FILE* file = fopen(config->filename, "w");
    if (!file) {
        return -1;
    }
    
    config_item_t* item = config->items;
    while (item) {
        fprintf(file, "%s=%s\n", item->key, item->value);
        item = item->next;
    }
    
    fclose(file);
    return 0;
}

const char* config_get(config_t* config, const char* key, const char* default_value) {
    if (!config || !key) {
        return default_value;
    }
    
    config_item_t* item = config->items;
    while (item) {
        if (strcmp(item->key, key) == 0) {
            return item->value;
        }
        item = item->next;
    }
    
    return default_value;
}

int config_get_int(config_t* config, const char* key, int default_value) {
    const char* value = config_get(config, key, NULL);
    if (!value) {
        return default_value;
    }
    
    return atoi(value);
}

int config_set(config_t* config, const char* key, const char* value) {
    if (!config || !key || !value) {
        return -1;
    }
    
    config_item_t* item = config->items;
    while (item) {
        if (strcmp(item->key, key) == 0) {
            strncpy(item->value, value, sizeof(item->value) - 1);
            return 0;
        }
        item = item->next;
    }
    
    item = (config_item_t*)malloc(sizeof(config_item_t));
    if (!item) {
        return -1;
    }
    
    strncpy(item->key, key, sizeof(item->key) - 1);
    strncpy(item->value, value, sizeof(item->value) - 1);
    item->next = config->items;
    config->items = item;
    
    return 0;
}

int config_set_int(config_t* config, const char* key, int value) {
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%d", value);
    return config_set(config, key, value_str);
}