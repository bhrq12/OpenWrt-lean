#include "config_adapter.h"
#include "../common/include/log.h"
#include "../sys_bus.h"
#include <string.h>
#include <stdlib.h>
#include <cjson/cJSON.h>
#include <cjson/cjson_schema.h>

static const char* config_schema = 
    "{"
    "\"type\": \"object\","
    "\"properties\": {"
    "\"config_id\": {\"type\": \"string\"},"
    "\"version\": {\"type\": \"integer\"},"
    "\"radios\": {"
    "\"type\": \"array\","
    "\"items\": {"
    "\"type\": \"object\","
    "\"properties\": {"
    "\"name\": {\"type\": \"string\"},"
    "\"channel\": {\"type\": \"integer\"},"
    "\"txpower\": {\"type\": \"integer\"}"
    "},"
    "\"required\": [\"name\"]"
    "}"
    "},"
    "\"interfaces\": {"
    "\"type\": \"array\","
    "\"items\": {"
    "\"type\": \"object\","
    "\"properties\": {"
    "\"name\": {\"type\": \"string\"},"
    "\"ssid\": {\"type\": \"string\"},"
    "\"encryption\": {\"type\": \"string\"},"
    "\"key\": {\"type\": \"string\"}"
    "},"
    "\"required\": [\"name\", \"ssid\"]"
    "}"
    "}"
    "},"
    "\"required\": [\"config_id\", \"version\"]"
    "}";

int config_adapter_init(config_adapter_t* adapter) {
    if (!adapter) {
        return -1;
    }
    
    memset(adapter, 0, sizeof(config_adapter_t));
    adapter->current_config.raw_config = NULL;
    adapter->previous_config.raw_config = NULL;
    adapter->rollback_count = 0;
    
    LOG_INFO("CFG_ADAPTER", "Config adapter initialized");
    return 0;
}

void config_adapter_destroy(config_adapter_t* adapter) {
    if (!adapter) {
        return;
    }
    
    if (adapter->current_config.raw_config) {
        free(adapter->current_config.raw_config);
        adapter->current_config.raw_config = NULL;
    }
    
    if (adapter->previous_config.raw_config) {
        free(adapter->previous_config.raw_config);
        adapter->previous_config.raw_config = NULL;
    }
    
    LOG_INFO("CFG_ADAPTER", "Config adapter destroyed");
}

adapter_result_t config_adapter_validate(const char* config_json) {
    if (!config_json) {
        return ADAPTER_RESULT_PARSE_ERROR;
    }
    
    cJSON* config = cJSON_Parse(config_json);
    if (!config) {
        LOG_ERROR("CFG_ADAPTER", "Failed to parse config JSON");
        return ADAPTER_RESULT_PARSE_ERROR;
    }
    
    cJSON* schema = cJSON_Parse(config_schema);
    if (!schema) {
        cJSON_Delete(config);
        LOG_ERROR("CFG_ADAPTER", "Failed to parse config schema");
        return ADAPTER_RESULT_VALIDATION_ERROR;
    }
    
    cJSON* result = cjson_schema_validate(config, schema);
    int valid = cJSON_IsTrue(result);
    
    cJSON_Delete(result);
    cJSON_Delete(schema);
    cJSON_Delete(config);
    
    if (!valid) {
        LOG_ERROR("CFG_ADAPTER", "Config validation failed");
        return ADAPTER_RESULT_VALIDATION_ERROR;
    }
    
    return ADAPTER_RESULT_SUCCESS;
}

adapter_result_t config_adapter_apply(config_adapter_t* adapter, const char* config_json) {
    if (!adapter || !config_json) {
        return ADAPTER_RESULT_PARSE_ERROR;
    }
    
    adapter_result_t ret = config_adapter_validate(config_json);
    if (ret != ADAPTER_RESULT_SUCCESS) {
        return ret;
    }
    
    cJSON* config = cJSON_Parse(config_json);
    if (!config) {
        return ADAPTER_RESULT_PARSE_ERROR;
    }
    
    cJSON* config_id = cJSON_GetObjectItem(config, "config_id");
    cJSON* version = cJSON_GetObjectItem(config, "version");
    
    if (!config_id || !cJSON_IsString(config_id) || !version || !cJSON_IsNumber(version)) {
        cJSON_Delete(config);
        return ADAPTER_RESULT_VALIDATION_ERROR;
    }
    
    if (adapter->current_config.raw_config) {
        if (adapter->previous_config.raw_config) {
            free(adapter->previous_config.raw_config);
        }
        adapter->previous_config = adapter->current_config;
        adapter->previous_config.raw_config = strdup(adapter->current_config.raw_config);
    }
    
    sys_bus_t bus;
    if (sys_bus_init(&bus) != 0) {
        cJSON_Delete(config);
        return ADAPTER_RESULT_APPLY_ERROR;
    }
    
    cJSON* radios = cJSON_GetObjectItem(config, "radios");
    if (cJSON_IsArray(radios)) {
        cJSON* radio;
        cJSON_ArrayForEach(radio, radios) {
            cJSON* name = cJSON_GetObjectItem(radio, "name");
            cJSON* channel = cJSON_GetObjectItem(radio, "channel");
            cJSON* txpower = cJSON_GetObjectItem(radio, "txpower");
            
            if (name && cJSON_IsString(name)) {
                int ch = channel && cJSON_IsNumber(channel) ? channel->valueint : 0;
                int tp = txpower && cJSON_IsNumber(txpower) ? txpower->valueint : 20;
                
                sys_bus_set_radio_config(&bus, name->valuestring, ch, tp);
            }
        }
    }
    
    cJSON* interfaces = cJSON_GetObjectItem(config, "interfaces");
    if (cJSON_IsArray(interfaces)) {
        cJSON* iface;
        cJSON_ArrayForEach(iface, interfaces) {
            cJSON* name = cJSON_GetObjectItem(iface, "name");
            cJSON* ssid = cJSON_GetObjectItem(iface, "ssid");
            cJSON* encryption = cJSON_GetObjectItem(iface, "encryption");
            cJSON* key = cJSON_GetObjectItem(iface, "key");
            
            if (name && cJSON_IsString(name) && ssid && cJSON_IsString(ssid)) {
                const char* enc = encryption && cJSON_IsString(encryption) ? encryption->valuestring : "psk2";
                const char* k = key && cJSON_IsString(key) ? key->valuestring : "";
                
                sys_bus_set_wifi_iface_config(&bus, name->valuestring, ssid->valuestring, enc, k);
            }
        }
    }
    
    sys_bus_reload_wifi(&bus);
    sys_bus_close(&bus);
    
    cJSON_Delete(config);
    
    strncpy(adapter->current_config.config_id, config_id->valuestring, sizeof(adapter->current_config.config_id) - 1);
    adapter->current_config.version = version->valueint;
    
    if (adapter->current_config.raw_config) {
        free(adapter->current_config.raw_config);
    }
    adapter->current_config.raw_config = strdup(config_json);
    
    LOG_INFO("CFG_ADAPTER", "Config applied: %s (v%d)", adapter->current_config.config_id, adapter->current_config.version);
    return ADAPTER_RESULT_SUCCESS;
}

adapter_result_t config_adapter_rollback(config_adapter_t* adapter) {
    if (!adapter) {
        return ADAPTER_RESULT_ROLLBACK_ERROR;
    }
    
    if (!adapter->previous_config.raw_config) {
        LOG_ERROR("CFG_ADAPTER", "No previous config to rollback");
        return ADAPTER_RESULT_ROLLBACK_ERROR;
    }
    
    adapter_result_t ret = config_adapter_apply(adapter, adapter->previous_config.raw_config);
    if (ret != ADAPTER_RESULT_SUCCESS) {
        return ret;
    }
    
    adapter->rollback_count++;
    LOG_INFO("CFG_ADAPTER", "Config rolled back, rollback count: %d", adapter->rollback_count);
    return ADAPTER_RESULT_SUCCESS;
}

int config_adapter_get_current_version(config_adapter_t* adapter) {
    if (!adapter) {
        return 0;
    }
    
    return adapter->current_config.version;
}

const char* config_adapter_get_current_config_id(config_adapter_t* adapter) {
    if (!adapter) {
        return NULL;
    }
    
    return adapter->current_config.config_id;
}