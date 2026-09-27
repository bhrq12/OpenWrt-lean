#include "ws_client.h"
#include "common/include/log.h"
#include "common/include/common.h"
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

void ws_client_set_callbacks(ws_client_t* client,
                             ws_client_connect_callback_t on_connect,
                             ws_client_disconnect_callback_t on_disconnect,
                             ws_client_message_callback_t on_message) {
    if (!client) {
        return;
    }
    client->on_connect = on_connect;
    client->on_disconnect = on_disconnect;
    client->on_message = on_message;
}

static void parse_ws_url(const char* url, char* scheme, int scheme_sz,
                         char* address, int addr_sz, int* port,
                         char* path, int path_sz, int* use_ssl) {
    const char* p = url;
    int ssl = 0;
    int pnum = 80;

    if (strncmp(p, "wss://", 6) == 0) {
        ssl = 1; pnum = 443; p += 6;
    } else if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    }
    if (scheme && scheme_sz > 0) {
        strncpy(scheme, ssl ? "wss" : "ws", scheme_sz - 1);
        scheme[scheme_sz - 1] = '\0';
    }

    const char* host_start = p;
    const char* port_colon = NULL;
    const char* path_slash = NULL;
    while (*p && *p != '/' && *p != ':') p++;
    if (*p == ':') { port_colon = p; p++; }
    int hlen = (port_colon ? (int)(port_colon - host_start) : (int)(p - host_start));
    hlen = hlen > addr_sz - 1 ? addr_sz - 1 : hlen;
    strncpy(address, host_start, hlen);
    address[hlen] = '\0';

    if (port_colon) {
        pnum = atoi(port_colon + 1);
        while (*p && *p != '/') p++;
    }
    if (*p == '/') {
        path_slash = p;
    }
    if (path_slash) {
        strncpy(path, path_slash, path_sz - 1);
        path[path_sz - 1] = '\0';
    } else {
        strncpy(path, "/", path_sz - 1);
        path[path_sz - 1] = '\0';
    }
    *port = pnum;
    *use_ssl = ssl;
}

int ws_client_start(ws_client_t* client) {
    if (!client || !client->context) {
        return -1;
    }

    client->running = 1;
    client->state = WS_CLIENT_STATE_CONNECTING;

    char address[256], path[256];
    int port, use_ssl;
    parse_ws_url(client->config.ac_url, NULL, 0, address, sizeof(address),
                 &port, path, sizeof(path), &use_ssl);

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context = (struct lws_context*)client->context;
    ccinfo.address = address;
    ccinfo.port = port;
    ccinfo.path = path;
    ccinfo.host = address;
    ccinfo.protocol = "ac-ap-protocol";
    ccinfo.pwsi = (struct lws**)&client->wsi;
    if (use_ssl) {
        ccinfo.ssl_connection = 1;
    }

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
            ccinfo.address = address;
            ccinfo.port = port;
            ccinfo.path = path;
            ccinfo.host = address;
            ccinfo.protocol = "ac-ap-protocol";
            ccinfo.pwsi = (struct lws**)&client->wsi;
            if (use_ssl) {
                ccinfo.ssl_connection = 1;
            }

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
                if (client->on_disconnect) {
                    client->on_disconnect(client, client->user_data);
                }
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            if (client) {
                client->wsi = wsi;
                client->state = WS_CLIENT_STATE_CONNECTED;
                LOG_INFO("WS_CLIENT", "Connected to AC");
                if (client->on_connect) {
                    client->on_connect(client, client->user_data);
                }
            }
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED: {
            if (client) {
                client->wsi = NULL;
                client->state = WS_CLIENT_STATE_DISCONNECTED;
                LOG_INFO("WS_CLIENT", "Disconnected from AC");
                if (client->on_disconnect) {
                    client->on_disconnect(client, client->user_data);
                }
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

            if (client->on_message) {
                client->on_message(client, NULL, NULL, buffer, client->user_data);
            }
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}