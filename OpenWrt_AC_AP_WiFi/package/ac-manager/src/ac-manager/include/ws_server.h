#ifndef WS_SERVER_H
#define WS_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/include/common.h"
#include <stdint.h>

typedef struct {
    int fd;
    char dev_sn[MAX_SN_LEN];
    void* user_data;
} ws_conn_t;

struct ws_client_conn_t;
typedef struct ws_client_conn_t ws_client_conn_t;

typedef void (*ws_connect_callback_t)(ws_conn_t* conn, void* user_data);
typedef void (*ws_disconnect_callback_t)(ws_conn_t* conn, void* user_data);
typedef void (*ws_message_callback_t)(ws_conn_t* conn, const char* msg_type,
                                      const char* msg_id, const char* payload, void* user_data);

typedef struct {
    const char* host;
    int port;
    const char* cert_file;
    const char* key_file;
    const char* ca_file;
    int max_connections;
    int heartbeat_interval;
    int idle_timeout;
} ws_server_config_t;

typedef struct {
    void* context;
    ws_server_config_t config;
    ws_connect_callback_t on_connect;
    ws_disconnect_callback_t on_disconnect;
    ws_message_callback_t on_message;
    void* user_data;
    int running;
    ws_client_conn_t* connections;
} ws_server_t;

int ws_server_init(ws_server_t* server, const ws_server_config_t* config);

int ws_server_start(ws_server_t* server);

void ws_server_stop(ws_server_t* server);

void ws_server_destroy(ws_server_t* server);

int ws_server_send(ws_server_t* server, ws_conn_t* conn, const char* msg_type,
                   const char* msg_id, const char* payload);

int ws_server_send_to_device(ws_server_t* server, const char* dev_sn, const char* msg_type,
                             const char* msg_id, const char* payload);

int ws_server_get_connection_count(ws_server_t* server);

#ifdef __cplusplus
}
#endif

#endif