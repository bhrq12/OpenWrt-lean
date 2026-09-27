#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/include/common.h"
#include <pthread.h>
#include <uthash.h>

typedef struct {
    char dev_sn[MAX_SN_LEN];
    char dev_model[MAX_MODEL_LEN];
    char fw_version[MAX_VERSION_LEN];
    char site_id[MAX_SITE_ID_LEN];
    char group_id[MAX_GROUP_ID_LEN];
    unsigned int ref_count;
    device_status_t status;
    char cur_config_id[MAX_CONFIG_ID_LEN];
    int config_version;
    time_t last_heartbeat;
    time_t register_time;
    int cpu_usage;
    int mem_usage;
    int client_count;
    void* conn_handle;
    UT_hash_handle hh;
} online_dev_t;

typedef struct {
    online_dev_t* devices;
    pthread_rwlock_t rwlock;
} device_manager_t;

int device_manager_init(device_manager_t* dm);

void device_manager_destroy(device_manager_t* dm);

int device_manager_add(device_manager_t* dm, const device_info_t* dev_info);

int device_manager_update_status(device_manager_t* dm, const char* dev_sn, device_status_t status);

int device_manager_update_heartbeat(device_manager_t* dm, const char* dev_sn, time_t heartbeat);

device_info_t* device_manager_get(device_manager_t* dm, const char* dev_sn);

int device_manager_remove(device_manager_t* dm, const char* dev_sn);

int device_manager_get_count(device_manager_t* dm);

int device_manager_get_online_count(device_manager_t* dm);

int device_manager_get_devices(device_manager_t* dm, device_info_t** devices, int* count);

int device_manager_check_timeout(device_manager_t* dm, time_t timeout_threshold,
                                 void (*callback)(const char* dev_sn, void* user_data),
                                 void* user_data);

#ifdef __cplusplus
}
#endif

#endif