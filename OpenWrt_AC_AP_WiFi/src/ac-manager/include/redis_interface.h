#ifndef REDIS_INTERFACE_H
#define REDIS_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/include/common.h"

typedef struct {
    void* handle;
    int connected;
    void* sub_handle;
    int sub_running;
    pthread_t sub_thread;
    redis_subscribe_callback_t callback;
    void* callback_data;
} redis_conn_t;

typedef void (*redis_subscribe_callback_t)(const char* channel, const char* message, void* user_data);

int redis_init(redis_conn_t* conn, const char* host, int port, const char* password);

void redis_close(redis_conn_t* conn);

int redis_set_device_online(redis_conn_t* conn, const char* dev_sn, device_status_t status, time_t ttl);

int redis_get_device_status(redis_conn_t* conn, const char* dev_sn);

int redis_set_config_version(redis_conn_t* conn, const char* config_id, int version);

int redis_get_config_version(redis_conn_t* conn, const char* config_id);

int redis_publish(redis_conn_t* conn, const char* channel, const char* message);

int redis_subscribe(redis_conn_t* conn, const char* channel, redis_subscribe_callback_t callback, void* user_data);

int redis_set(redis_conn_t* conn, const char* key, const char* value, time_t ttl);

int redis_get(redis_conn_t* conn, const char* key, char* value, int value_len);

int redis_del(redis_conn_t* conn, const char* key);

int redis_incr(redis_conn_t* conn, const char* key, int* result);

int redis_decr(redis_conn_t* conn, const char* key, int* result);

#ifdef __cplusplus
}
#endif

#endif