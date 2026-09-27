#include "config_manager.h"
#include "../common/include/log.h"
#include <string.h>
#include <stdlib.h>
#include <openssl/sha.h>

int config_manager_init(config_manager_t* cm) {
    if (!cm) {
        return -1;
    }
    
    memset(cm, 0, sizeof(config_manager_t));
    cm->templates = NULL;
    
    if (pthread_rwlock_init(&cm->rwlock, NULL) != 0) {
        LOG_ERROR("CFG_MGR", "Failed to init rwlock");
        return -1;
    }
    
    LOG_INFO("CFG_MGR", "Config manager initialized");
    return 0;
}

void config_manager_destroy(config_manager_t* cm) {
    if (!cm) {
        return;
    }
    
    config_template_entry_t* template, *tmp;
    pthread_rwlock_wrlock(&cm->rwlock);
    HASH_ITER(hh, cm->templates, template, tmp) {
        HASH_DEL(cm->templates, template);
        if (template->config_content) {
            free(template->config_content);
        }
        free(template);
    }
    pthread_rwlock_unlock(&cm->rwlock);
    
    pthread_rwlock_destroy(&cm->rwlock);
}

int config_manager_add_template(config_manager_t* cm, const config_template_t* template) {
    if (!cm || !template || !template->config_id[0]) {
        return -1;
    }
    
    config_template_entry_t* entry = (config_template_entry_t*)malloc(sizeof(config_template_entry_t));
    if (!entry) {
        LOG_ERROR("CFG_MGR", "Failed to allocate template entry");
        return -1;
    }
    
    memset(entry, 0, sizeof(config_template_entry_t));
    strncpy(entry->config_id, template->config_id, sizeof(entry->config_id) - 1);
    strncpy(entry->template_name, template->template_name, sizeof(entry->template_name) - 1);
    entry->template_type = template->template_type;
    entry->version = template->version;
    strncpy(entry->creator, template->creator, sizeof(entry->creator) - 1);
    entry->create_time = template->create_time;
    
    if (template->config_content) {
        entry->config_content = strdup(template->config_content);
        if (!entry->config_content) {
            free(entry);
            LOG_ERROR("CFG_MGR", "Failed to duplicate config content");
            return -1;
        }
        
        config_manager_generate_signature(template->config_content, entry->signature, sizeof(entry->signature));
    }
    
    pthread_rwlock_wrlock(&cm->rwlock);
    config_template_entry_t* existing;
    HASH_FIND_STR(cm->templates, template->config_id, existing);
    if (existing) {
        pthread_rwlock_unlock(&cm->rwlock);
        free(entry->config_content);
        free(entry);
        LOG_WARN("CFG_MGR", "Template already exists: %s", template->config_id);
        return -1;
    }
    HASH_ADD_STR(cm->templates, config_id, entry);
    pthread_rwlock_unlock(&cm->rwlock);
    
    LOG_INFO("CFG_MGR", "Template added: %s (%s)", entry->config_id, entry->template_name);
    return 0;
}

int config_manager_update_template(config_manager_t* cm, const config_template_t* template) {
    if (!cm || !template || !template->config_id[0]) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&cm->rwlock);
    config_template_entry_t* entry;
    HASH_FIND_STR(cm->templates, template->config_id, entry);
    if (!entry) {
        pthread_rwlock_unlock(&cm->rwlock);
        LOG_WARN("CFG_MGR", "Template not found: %s", template->config_id);
        return -1;
    }
    
    strncpy(entry->template_name, template->template_name, sizeof(entry->template_name) - 1);
    entry->template_type = template->template_type;
    entry->version = template->version;
    strncpy(entry->creator, template->creator, sizeof(entry->creator) - 1);
    entry->create_time = template->create_time;
    
    if (template->config_content) {
        if (entry->config_content) {
            free(entry->config_content);
        }
        entry->config_content = strdup(template->config_content);
        if (!entry->config_content) {
            pthread_rwlock_unlock(&cm->rwlock);
            LOG_ERROR("CFG_MGR", "Failed to duplicate config content");
            return -1;
        }
        
        config_manager_generate_signature(template->config_content, entry->signature, sizeof(entry->signature));
    }
    
    pthread_rwlock_unlock(&cm->rwlock);
    
    LOG_INFO("CFG_MGR", "Template updated: %s (version: %d)", entry->config_id, entry->version);
    return 0;
}

config_template_t* config_manager_get_template(config_manager_t* cm, const char* config_id) {
    if (!cm || !config_id) {
        return NULL;
    }
    
    pthread_rwlock_rdlock(&cm->rwlock);
    config_template_entry_t* entry;
    HASH_FIND_STR(cm->templates, config_id, entry);
    
    config_template_t* result = NULL;
    if (entry) {
        result = (config_template_t*)malloc(sizeof(config_template_t));
        if (result) {
            memset(result, 0, sizeof(config_template_t));
            strncpy(result->config_id, entry->config_id, sizeof(result->config_id) - 1);
            strncpy(result->template_name, entry->template_name, sizeof(result->template_name) - 1);
            result->template_type = entry->template_type;
            if (entry->config_content) {
                result->config_content = strdup(entry->config_content);
            }
            strncpy(result->signature, entry->signature, sizeof(result->signature) - 1);
            result->version = entry->version;
            strncpy(result->creator, entry->creator, sizeof(result->creator) - 1);
            result->create_time = entry->create_time;
        }
    }
    pthread_rwlock_unlock(&cm->rwlock);
    
    return result;
}

int config_manager_delete_template(config_manager_t* cm, const char* config_id) {
    if (!cm || !config_id) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&cm->rwlock);
    config_template_entry_t* entry;
    HASH_FIND_STR(cm->templates, config_id, entry);
    if (!entry) {
        pthread_rwlock_unlock(&cm->rwlock);
        LOG_WARN("CFG_MGR", "Template not found: %s", config_id);
        return -1;
    }
    
    HASH_DEL(cm->templates, entry);
    if (entry->config_content) {
        free(entry->config_content);
    }
    free(entry);
    pthread_rwlock_unlock(&cm->rwlock);
    
    LOG_INFO("CFG_MGR", "Template deleted: %s", config_id);
    return 0;
}

int config_manager_get_templates(config_manager_t* cm, config_template_t** templates, int* count) {
    if (!cm || !templates || !count) {
        return -1;
    }
    
    pthread_rwlock_rdlock(&cm->rwlock);
    int total = HASH_COUNT(cm->templates);
    *templates = (config_template_t*)malloc(total * sizeof(config_template_t));
    if (!*templates) {
        pthread_rwlock_unlock(&cm->rwlock);
        return -1;
    }
    
    config_template_entry_t* entry, *tmp;
    int idx = 0;
    HASH_ITER(hh, cm->templates, entry, tmp) {
        memset(&(*templates)[idx], 0, sizeof(config_template_t));
        strncpy((*templates)[idx].config_id, entry->config_id, sizeof((*templates)[idx].config_id) - 1);
        strncpy((*templates)[idx].template_name, entry->template_name, sizeof((*templates)[idx].template_name) - 1);
        (*templates)[idx].template_type = entry->template_type;
        (*templates)[idx].version = entry->version;
        (*templates)[idx].create_time = entry->create_time;
        idx++;
    }
    *count = total;
    pthread_rwlock_unlock(&cm->rwlock);
    
    return 0;
}

int config_manager_get_templates_by_type(config_manager_t* cm, int template_type,
                                         config_template_t** templates, int* count) {
    if (!cm || !templates || !count) {
        return -1;
    }
    
    pthread_rwlock_rdlock(&cm->rwlock);
    config_template_entry_t* entry, *tmp;
    int total = 0;
    
    HASH_ITER(hh, cm->templates, entry, tmp) {
        if (entry->template_type == template_type) {
            total++;
        }
    }
    
    *templates = (config_template_t*)malloc(total * sizeof(config_template_t));
    if (!*templates) {
        pthread_rwlock_unlock(&cm->rwlock);
        return -1;
    }
    
    int idx = 0;
    HASH_ITER(hh, cm->templates, entry, tmp) {
        if (entry->template_type == template_type) {
            memset(&(*templates)[idx], 0, sizeof(config_template_t));
            strncpy((*templates)[idx].config_id, entry->config_id, sizeof((*templates)[idx].config_id) - 1);
            strncpy((*templates)[idx].template_name, entry->template_name, sizeof((*templates)[idx].template_name) - 1);
            (*templates)[idx].template_type = entry->template_type;
            (*templates)[idx].version = entry->version;
            (*templates)[idx].create_time = entry->create_time;
            idx++;
        }
    }
    *count = total;
    pthread_rwlock_unlock(&cm->rwlock);
    
    return 0;
}

int config_manager_generate_signature(const char* config_content, char* signature, int signature_len) {
    if (!config_content || !signature || signature_len < 65) {
        return -1;
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)config_content, strlen(config_content), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        snprintf(signature + i * 2, 3, "%02x", hash[i]);
    }
    signature[64] = '\0';
    
    return 0;
}

int config_manager_verify_signature(const char* config_content, const char* signature) {
    if (!config_content || !signature) {
        return -1;
    }
    
    char computed[256];
    if (config_manager_generate_signature(config_content, computed, sizeof(computed)) != 0) {
        return -1;
    }
    
    return strcmp(computed, signature) == 0 ? 0 : -1;
}