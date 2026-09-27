#include "ws_client.h"
#include "../common/include/log.h"
#include "../common/include/common.h"
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MAX_PAYLOAD 16384

static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);

int ws_client_init(ws_client_t* client, const ws_client_config_t* config) {
    if (!client || !config || !config->ac_url) {
        return -1;
    }
    
    memset(client, 0, sizeof(ws_client_t));
    memcpy(&client->config, config, sizeof(ws_client_config_t));
    client->state = WS_CLIENT_STATE_DISCONNECTED;
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.protocols = (struct lws_protocols[]) {
        {
            "ac-ap-protocol",
            ws_client_callback,
            sizeof(ws_client_t*),
            MAX_PAYLOAD,
        },
        { NULL, NULL, 0, 0 }
    };
    
    if (config->cert_file && config->key_file) {
        info.ssl_cert_filepath = config->cert_file;
        info.ssl_private_key_filepath = config->key_file;
        info.ssl_ca_filepath = config->ca_file;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }
    
    info.gid = -1;
    info.uid = -1;
    
    client->context = lws_create_context(&info);
    if (!client->context) {
        LOG_ERROR("WS_CLIENT", "Failed to create websocket context");
        return -1;
    }
    
    LOG_INFO("WS_CLIENT", "WebSocket client initialized for %s", config->ac_url);
    return 0;
}

int ws_client_start(ws_client_t* client) {
    if (!client || !client->context) {
        return -1;
    }
    
    client->running = 1;
    client->state = WS_CLIENT_STATE_CONNECTING;
    
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    
    ccinfo.context = (struct lws_context*)client->context;
    ccinfo.uri = client->config.ac_url;
    ccinfo.protocol = "ac-ap-protocol";
    ccinfo.pwsi = &client->wsi;
    
    if (!lws_client_connect_via_info(&ccinfo)) {
        LOG_ERROR("WS_CLIENT", "Failed to connect to AC");
        client->state = WS_CLIENT_STATE_DISCONNECTED;
        return -1;
    }
    
    LOG_INFO("WS_CLIENT", "Connecting to AC at %s", client->config.ac_url);
    
    while (client->running && lws_service((struct lws_context*)client->context, 1000) >= 0) {
        if (client->state == WS_CLIENT_STATE_DISCONNECTED && client->running) {
            sleep(client->config.reconnect_interval);
            
            LOG_INFO("WS_CLIENT", "Reconnecting to AC...");
            client->state = WS_CLIENT_STATE_RECONNECTING;
            
            memset(&ccinfo, 0, sizeof(ccinfo));
            ccinfo.context = (struct lws_context*)client->context;
            ccinfo.uri = client->config.ac_url;
            ccinfo.protocol = "ac-ap-protocol";
            ccinfo.pwsi = &client->wsi;
            
            if (lws_client_connect_via_info(&ccinfo)) {
                client->state = WS_CLIENT_STATE_CONNECTING;
            }
        }
    }
    
    return 0;
}

void ws_client_stop(ws_client_t* client) {
    if (!client) {
        return;
    }
    
    client->running = 0;
}

void ws_client_destroy(ws_client_t* client) {
    if (!client) {
        return;
    }
    
    if (client->context) {
        lws_context_destroy((struct lws_context*)client->context);
        client->context = NULL;
    }
    
    LOG_INFO("WS_CLIENT", "WebSocket client destroyed");
}

int ws_client_send(ws_client_t* client, const char* msg_type, const char* msg_id, const char* payload) {
    if (!client || !client->wsi || !msg_type) {
        return -1;
    }
    
    char buffer[MAX_PAYLOAD];
    int len = snprintf(buffer, sizeof(buffer),
                       "{\"type\":\"%s\",\"msgid\":\"%s\",\"payload\":%s}",
                       msg_type, msg_id ? msg_id : "", payload ? payload : "null");
    
    unsigned char* buf = malloc(LWS_PRE + len);
    if (!buf) {
        return -1;
    }
    
    memcpy(buf + LWS_PRE, buffer, len);
    
    int ret = lws_write((struct lws*)client->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);
    
    if (ret < 0) {
        LOG_ERROR("WS_CLIENT", "Failed to send message");
        return -1;
    }
    
    LOG_DEBUG("WS_CLIENT", "Sent message: %s", msg_type);
    return 0;
}

ws_client_state_t ws_client_get_state(ws_client_t* client) {
    if (!client) {
        return WS_CLIENT_STATE_DISCONNECTED;
    }
    
    return client->state;
}

static int ws_client_callback(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len) {
    ws_client_t** client_ptr = (ws_client_t**)user;
    ws_client_t* client = *client_ptr;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            if (client) {
                client->state = WS_CLIENT_STATE_DISCONNECTED;
                LOG_ERROR("WS_CLIENT", "Connection error");
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            if (client) {
                client->wsi = wsi;
                client->state = WS_CLIENT_STATE_CONNECTED;
                LOG_INFO("WS_CLIENT", "Connected to AC");
            }
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CLOSED: {
            if (client) {
                client->wsi = NULL;
                client->state = WS_CLIENT_STATE_DISCONNECTED;
                LOG_INFO("WS_CLIENT", "Disconnected from AC");
            }
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            if (!client || len == 0) {
                break;
            }
            
            char buffer[MAX_PAYLOAD + 1];
            memset(buffer, 0, sizeof(buffer));
            len = len > MAX_PAYLOAD ? MAX_PAYLOAD : len;
            memcpy(buffer, in, len);
            
            LOG_DEBUG("WS_CLIENT", "Received message: %s", buffer);
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}