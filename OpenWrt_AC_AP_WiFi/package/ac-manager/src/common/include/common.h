#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_SN_LEN 32
#define MAX_MODEL_LEN 64
#define MAX_VERSION_LEN 32
#define MAX_SITE_ID_LEN 32
#define MAX_GROUP_ID_LEN 32
#define MAX_CONFIG_ID_LEN 32
#define MAX_MSG_ID_LEN 64
#define MAX_MSG_TYPE_LEN 64
#define MAX_ERROR_MSG_LEN 256

typedef enum {
    DEV_OFFLINE = 0,
    DEV_ONLINE = 1,
    DEV_UPGRADING = 2,
    DEV_MAINTENANCE = 3,
    DEV_DISCONNECTED = 4
} device_status_t;

typedef enum {
    MSG_PRIORITY_URGENT = 0,
    MSG_PRIORITY_HIGH = 1,
    MSG_PRIORITY_NORMAL = 2,
    MSG_PRIORITY_LOW = 3
} msg_priority_t;

typedef enum {
    ERROR_SUCCESS = 0,
    ERROR_CONFIG = 1000,
    ERROR_AUTH = 1100,
    ERROR_SYSTEM = 1200,
    ERROR_NETWORK = 1300,
    ERROR_BUSINESS = 1400
} error_code_t;

typedef struct {
    char msgid[MAX_MSG_ID_LEN];
    char type[MAX_MSG_TYPE_LEN];
    msg_priority_t priority;
    time_t timestamp;
    char dev_sn[MAX_SN_LEN];
    char content_encoding[16];
    char* payload;
    int error_code;
    char error_message[MAX_ERROR_MSG_LEN];
} msg_t;

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
} device_info_t;

typedef struct {
    char config_id[MAX_CONFIG_ID_LEN];
    char template_name[MAX_MODEL_LEN];
    int template_type;
    char* config_content;
    char signature[256];
    int version;
    char creator[MAX_MODEL_LEN];
    time_t create_time;
} config_template_t;

char* generate_msgid(const char* dev_sn);

void free_msg(msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif