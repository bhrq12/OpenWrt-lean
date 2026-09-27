#ifndef DB_INTERFACE_H
#define DB_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/include/common.h"

typedef enum {
    DB_TYPE_SQLITE = 1,
    DB_TYPE_MYSQL = 2
} db_type_t;

typedef struct {
    db_type_t type;
    void* handle;
} db_conn_t;

typedef struct {
    char dev_sn[MAX_SN_LEN];
    char dev_model[MAX_MODEL_LEN];
    char fw_version[MAX_VERSION_LEN];
    char site_id[MAX_SITE_ID_LEN];
    char group_id[MAX_GROUP_ID_LEN];
    device_status_t status;
    char cur_config_id[MAX_CONFIG_ID_LEN];
    int config_version;
    time_t register_time;
    time_t last_heartbeat;
} db_device_t;

typedef struct {
    char config_id[MAX_CONFIG_ID_LEN];
    char template_name[MAX_MODEL_LEN];
    int template_type;
    char* config_content;
    char signature[256];
    int version;
    char creator[MAX_MODEL_LEN];
    time_t create_time;
} db_config_template_t;

int db_init(db_conn_t* conn, db_type_t type, const char* host, int port,
            const char* db_name, const char* user, const char* password);

void db_close(db_conn_t* conn);

int db_create_tables(db_conn_t* conn);

int db_save_device(db_conn_t* conn, const db_device_t* device);

int db_update_device_status(db_conn_t* conn, const char* dev_sn, device_status_t status);

int db_update_device_heartbeat(db_conn_t* conn, const char* dev_sn, time_t heartbeat);

int db_get_device(db_conn_t* conn, const char* dev_sn, db_device_t* device);

int db_get_devices_by_site(db_conn_t* conn, const char* site_id, db_device_t** devices, int* count);

int db_get_devices_by_status(db_conn_t* conn, device_status_t status, db_device_t** devices, int* count);

int db_delete_device(db_conn_t* conn, const char* dev_sn);

int db_save_config_template(db_conn_t* conn, const db_config_template_t* template);

int db_update_config_template(db_conn_t* conn, const db_config_template_t* template);

int db_get_config_template(db_conn_t* conn, const char* config_id, db_config_template_t* template);

int db_get_config_templates(db_conn_t* conn, db_config_template_t** templates, int* count);

int db_get_config_templates_by_type(db_conn_t* conn, int template_type,
                                    db_config_template_t** templates, int* count);

int db_delete_config_template(db_conn_t* conn, const char* config_id);

void db_free_devices(db_device_t* devices, int count);

void db_free_config_templates(db_config_template_t* templates, int count);

#ifdef __cplusplus
}
#endif

#endif