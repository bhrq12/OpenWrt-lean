#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/include/common.h"
#include <stdint.h>

typedef enum {
    WS_CLIENT_STATE_DISCONNECTED = 0,
    WS_CLIENT_STATE_CONNECTING = 1,
    WS_CLIENT_STATE_CONNECTED = 2,
    WS_CLIENT_STATE_RECONNECTING = 3
} ws_client_state_t;

typedef struct {
    const char* ac_url;
    const char* cert_file;
    const char* key_file;
    const char* ca_file;
    int reconnect_interval;
    int heartbeat_interval;
} ws_client_config_t;

typedef struct {
    void* context;
    void* wsi;
    ws_client_config_t config;
    ws_client_state_t state;
    int running;
} ws_client_t;

typedef void (*ws_client_connect_callback_t)(ws_client_t* client, void* user_data);
typedef void (*ws_client_disconnect_callback_t)(ws_client_t* client, void* user_data);
typedef void (*ws_client_message_callback_t)(ws_client_t* client, const char* msg_type,
                                             const char* msg_id, const char* payload, void* user_data);

int ws_client_init(ws_client_t* client, const ws_client_config_t* config);

int ws_client_start(ws_client_t* client);

void ws_client_stop(ws_client_t* client);

void ws_client_destroy(ws_client_t* client);

int ws_client_send(ws_client_t* client, const char* msg_type, const char* msg_id, const char* payload);

ws_client_state_t ws_client_get_state(ws_client_t* client);

#ifdef __cplusplus
}
#endif

#endif