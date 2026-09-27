#include "device_manager.h"
#include "common/include/log.h"
#include <string.h>
#include <stdlib.h>

int device_manager_init(device_manager_t* dm) {
    if (!dm) {
        return -1;
    }
    
    memset(dm, 0, sizeof(device_manager_t));
    dm->devices = NULL;
    
    if (pthread_rwlock_init(&dm->rwlock, NULL) != 0) {
        LOG_ERROR("DEV_MGR", "Failed to init rwlock");
        return -1;
    }
    
    LOG_INFO("DEV_MGR", "Device manager initialized");
    return 0;
}

void device_manager_destroy(device_manager_t* dm) {
    if (!dm) {
        return;
    }
    
    online_dev_t* dev, *tmp;
    pthread_rwlock_wrlock(&dm->rwlock);
    HASH_ITER(hh, dm->devices, dev, tmp) {
        HASH_DEL(dm->devices, dev);
        free(dev);
    }
    pthread_rwlock_unlock(&dm->rwlock);
    
    pthread_rwlock_destroy(&dm->rwlock);
}

int device_manager_add(device_manager_t* dm, const device_info_t* dev_info) {
    if (!dm || !dev_info) {
        return -1;
    }
    
    online_dev_t* dev = (online_dev_t*)malloc(sizeof(online_dev_t));
    if (!dev) {
        LOG_ERROR("DEV_MGR", "Failed to allocate device");
        return -1;
    }
    
    memset(dev, 0, sizeof(online_dev_t));
    strncpy(dev->dev_sn, dev_info->dev_sn, sizeof(dev->dev_sn) - 1);
    strncpy(dev->dev_model, dev_info->dev_model, sizeof(dev->dev_model) - 1);
    strncpy(dev->fw_version, dev_info->fw_version, sizeof(dev->fw_version) - 1);
    strncpy(dev->site_id, dev_info->site_id, sizeof(dev->site_id) - 1);
    strncpy(dev->group_id, dev_info->group_id, sizeof(dev->group_id) - 1);
    dev->ref_count = 1;
    dev->status = dev_info->status;
    strncpy(dev->cur_config_id, dev_info->cur_config_id, sizeof(dev->cur_config_id) - 1);
    dev->config_version = dev_info->config_version;
    dev->last_heartbeat = dev_info->last_heartbeat;
    dev->register_time = dev_info->register_time;
    dev->cpu_usage = dev_info->cpu_usage;
    dev->mem_usage = dev_info->mem_usage;
    dev->client_count = dev_info->client_count;
    
    pthread_rwlock_wrlock(&dm->rwlock);
    online_dev_t* existing;
    HASH_FIND_STR(dm->devices, dev_info->dev_sn, existing);
    if (existing) {
        HASH_DEL(dm->devices, existing);
        free(existing);
    }
    HASH_ADD_STR(dm->devices, dev_sn, dev);
    pthread_rwlock_unlock(&dm->rwlock);
    
    LOG_INFO("DEV_MGR", "Device added: %s (%s)", dev->dev_sn, dev->dev_model);
    return 0;
}

int device_manager_update_status(device_manager_t* dm, const char* dev_sn, device_status_t status) {
    if (!dm || !dev_sn) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&dm->rwlock);
    online_dev_t* dev;
    HASH_FIND_STR(dm->devices, dev_sn, dev);
    if (!dev) {
        pthread_rwlock_unlock(&dm->rwlock);
        LOG_WARN("DEV_MGR", "Device not found: %s", dev_sn);
        return -1;
    }
    
    dev->status = status;
    pthread_rwlock_unlock(&dm->rwlock);
    
    LOG_DEBUG("DEV_MGR", "Device %s status updated to %d", dev_sn, status);
    return 0;
}

int device_manager_update_heartbeat(device_manager_t* dm, const char* dev_sn, time_t heartbeat) {
    if (!dm || !dev_sn) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&dm->rwlock);
    online_dev_t* dev;
    HASH_FIND_STR(dm->devices, dev_sn, dev);
    if (!dev) {
        pthread_rwlock_unlock(&dm->rwlock);
        LOG_WARN("DEV_MGR", "Device not found: %s", dev_sn);
        return -1;
    }
    
    dev->last_heartbeat = heartbeat;
    pthread_rwlock_unlock(&dm->rwlock);
    
    return 0;
}

device_info_t* device_manager_get(device_manager_t* dm, const char* dev_sn) {
    if (!dm || !dev_sn) {
        return NULL;
    }
    
    pthread_rwlock_rdlock(&dm->rwlock);
    online_dev_t* dev;
    HASH_FIND_STR(dm->devices, dev_sn, dev);
    
    device_info_t* result = NULL;
    if (dev) {
        result = (device_info_t*)malloc(sizeof(device_info_t));
        if (result) {
            memset(result, 0, sizeof(device_info_t));
            strncpy(result->dev_sn, dev->dev_sn, sizeof(result->dev_sn) - 1);
            strncpy(result->dev_model, dev->dev_model, sizeof(result->dev_model) - 1);
            strncpy(result->fw_version, dev->fw_version, sizeof(result->fw_version) - 1);
            strncpy(result->site_id, dev->site_id, sizeof(result->site_id) - 1);
            strncpy(result->group_id, dev->group_id, sizeof(result->group_id) - 1);
            result->ref_count = dev->ref_count;
            result->status = dev->status;
            strncpy(result->cur_config_id, dev->cur_config_id, sizeof(result->cur_config_id) - 1);
            result->config_version = dev->config_version;
            result->last_heartbeat = dev->last_heartbeat;
            result->register_time = dev->register_time;
            result->cpu_usage = dev->cpu_usage;
            result->mem_usage = dev->mem_usage;
            result->client_count = dev->client_count;
        }
    }
    pthread_rwlock_unlock(&dm->rwlock);
    
    return result;
}

int device_manager_remove(device_manager_t* dm, const char* dev_sn) {
    if (!dm || !dev_sn) {
        return -1;
    }
    
    pthread_rwlock_wrlock(&dm->rwlock);
    online_dev_t* dev;
    HASH_FIND_STR(dm->devices, dev_sn, dev);
    if (!dev) {
        pthread_rwlock_unlock(&dm->rwlock);
        LOG_WARN("DEV_MGR", "Device not found: %s", dev_sn);
        return -1;
    }
    
    HASH_DEL(dm->devices, dev);
    free(dev);
    pthread_rwlock_unlock(&dm->rwlock);
    
    LOG_INFO("DEV_MGR", "Device removed: %s", dev_sn);
    return 0;
}

int device_manager_get_count(device_manager_t* dm) {
    if (!dm) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&dm->rwlock);
    int count = HASH_COUNT(dm->devices);
    pthread_rwlock_unlock(&dm->rwlock);
    
    return count;
}

int device_manager_get_online_count(device_manager_t* dm) {
    if (!dm) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&dm->rwlock);
    online_dev_t* dev, *tmp;
    int count = 0;
    HASH_ITER(hh, dm->devices, dev, tmp) {
        if (dev->status == DEV_ONLINE) {
            count++;
        }
    }
    pthread_rwlock_unlock(&dm->rwlock);
    
    return count;
}

int device_manager_get_devices(device_manager_t* dm, device_info_t** devices, int* count) {
    if (!dm || !devices || !count) {
        return -1;
    }
    
    pthread_rwlock_rdlock(&dm->rwlock);
    int total = HASH_COUNT(dm->devices);
    *devices = (device_info_t*)malloc(total * sizeof(device_info_t));
    if (!*devices) {
        pthread_rwlock_unlock(&dm->rwlock);
        return -1;
    }
    
    online_dev_t* dev, *tmp;
    int idx = 0;
    HASH_ITER(hh, dm->devices, dev, tmp) {
        memset(&(*devices)[idx], 0, sizeof(device_info_t));
        strncpy((*devices)[idx].dev_sn, dev->dev_sn, sizeof((*devices)[idx].dev_sn) - 1);
        strncpy((*devices)[idx].dev_model, dev->dev_model, sizeof((*devices)[idx].dev_model) - 1);
        strncpy((*devices)[idx].fw_version, dev->fw_version, sizeof((*devices)[idx].fw_version) - 1);
        strncpy((*devices)[idx].site_id, dev->site_id, sizeof((*devices)[idx].site_id) - 1);
        strncpy((*devices)[idx].group_id, dev->group_id, sizeof((*devices)[idx].group_id) - 1);
        (*devices)[idx].status = dev->status;
        (*devices)[idx].last_heartbeat = dev->last_heartbeat;
        (*devices)[idx].cpu_usage = dev->cpu_usage;
        (*devices)[idx].mem_usage = dev->mem_usage;
        (*devices)[idx].client_count = dev->client_count;
        idx++;
    }
    *count = total;
    pthread_rwlock_unlock(&dm->rwlock);
    
    return 0;
}

int device_manager_check_timeout(device_manager_t* dm, time_t timeout_threshold,
                                 void (*callback)(const char* dev_sn, void* user_data),
                                 void* user_data) {
    if (!dm || !callback) {
        return -1;
    }
    
    time_t now = time(NULL);
    pthread_rwlock_wrlock(&dm->rwlock);
    
    online_dev_t* dev, *tmp;
    int timeout_count = 0;
    HASH_ITER(hh, dm->devices, dev, tmp) {
        if (dev->status == DEV_ONLINE && (now - dev->last_heartbeat) > timeout_threshold) {
            timeout_count++;
            callback(dev->dev_sn, user_data);
        }
    }
    
    pthread_rwlock_unlock(&dm->rwlock);
    
    return timeout_count;
}