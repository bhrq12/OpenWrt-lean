#include "ws_server.h"
#include "common/include/log.h"
#include "common/include/common.h"
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <uthash.h>
#include <cjson/cJSON.h>

#define MAX_PAYLOAD 16384
#define MAX_CONNECTIONS 1024

struct ws_client_conn_t {
    struct lws* wsi;
    char dev_sn[MAX_SN_LEN];
    ws_server_t* server;
    time_t last_activity;
    UT_hash_handle hh;
};

static pthread_mutex_t conn_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len);

int ws_server_init(ws_server_t* server, const ws_server_config_t* config) {
    if (!server || !config) {
        return -1;
    }
    
    memset(server, 0, sizeof(ws_server_t));
    memcpy(&server->config, config, sizeof(ws_server_config_t));
    server->connections = NULL;
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = config->port;
    info.protocols = (struct lws_protocols[]) {
        {
            "ac-ap-protocol",
            ws_callback,
            sizeof(ws_client_conn_t),
            MAX_PAYLOAD,
        },
        { NULL, NULL, 0, 0 }
    };
    
    if (config->cert_file && config->key_file) {
        info.ssl_cert_filepath = config->cert_file;
        info.ssl_private_key_filepath = config->key_file;
        info.ssl_ca_filepath = config->ca_file;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.options |= LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT;
    }
    
    info.max_http_header_pool = 16;
    info.max_http_header_data = 4096;
    info.gid = -1;
    info.uid = -1;
    
    server->context = lws_create_context(&info);
    if (!server->context) {
        LOG_ERROR("WS_SERVER", "Failed to create websocket context");
        return -1;
    }
    
    LOG_INFO("WS_SERVER", "WebSocket server initialized on %s:%d", config->host ? config->host : "0.0.0.0", config->port);
    return 0;
}

int ws_server_start(ws_server_t* server) {
    if (!server || !server->context) {
        return -1;
    }
    
    server->running = 1;
    
    while (server->running && lws_service((struct lws_context*)server->context, 1000) >= 0) {
    }
    
    LOG_INFO("WS_SERVER", "WebSocket server stopped");
    return 0;
}

void ws_server_stop(ws_server_t* server) {
    if (!server) {
        return;
    }
    
    server->running = 0;
}

void ws_server_destroy(ws_server_t* server) {
    if (!server) {
        return;
    }
    
    if (server->context) {
        lws_context_destroy((struct lws_context*)server->context);
        server->context = NULL;
    }
    
    LOG_INFO("WS_SERVER", "WebSocket server destroyed");
}

int ws_server_send(ws_server_t* server, ws_conn_t* conn, const char* msg_type,
                   const char* msg_id, const char* payload) {
    if (!server || !conn || !msg_type) {
        return -1;
    }
    
    ws_client_conn_t* client = (ws_client_conn_t*)conn->user_data;
    if (!client || !client->wsi) {
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
    
    int ret = lws_write(client->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);
    
    if (ret < 0) {
        LOG_ERROR("WS_SERVER", "Failed to send message to %s", conn->dev_sn);
        return -1;
    }
    
    LOG_DEBUG("WS_SERVER", "Sent message to %s: %s", conn->dev_sn, msg_type);
    return 0;
}

int ws_server_send_to_device(ws_server_t* server, const char* dev_sn, const char* msg_type,
                             const char* msg_id, const char* payload) {
    if (!server || !dev_sn || !msg_type) {
        return -1;
    }
    
    pthread_mutex_lock(&conn_mutex);
    
    ws_client_conn_t* client = NULL;
    HASH_FIND_STR(server->connections, dev_sn, client);
    
    if (client && client->wsi) {
        ws_conn_t conn;
        conn.fd = lws_get_socket_fd(client->wsi);
        strncpy(conn.dev_sn, dev_sn, sizeof(conn.dev_sn) - 1);
        conn.user_data = client;
        
        pthread_mutex_unlock(&conn_mutex);
        return ws_server_send(server, &conn, msg_type, msg_id, payload);
    }
    
    pthread_mutex_unlock(&conn_mutex);
    
    LOG_WARN("WS_SERVER", "No connection found for device: %s", dev_sn);
    return -1;
}

int ws_server_get_connection_count(ws_server_t* server) {
    if (!server) {
        return 0;
    }
    
    pthread_mutex_lock(&conn_mutex);
    int count = HASH_COUNT(server->connections);
    pthread_mutex_unlock(&conn_mutex);
    
    return count;
}

static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason,
                       void* user, void* in, size_t len) {
    ws_client_conn_t* client = (ws_client_conn_t*)user;
    ws_server_t* server = client ? client->server : NULL;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            memset(client->dev_sn, 0, sizeof(client->dev_sn));
            client->wsi = wsi;
            client->last_activity = time(NULL);
            client->server = server;
            
            pthread_mutex_lock(&conn_mutex);
            HASH_ADD_STR(server->connections, dev_sn, client);
            pthread_mutex_unlock(&conn_mutex);
            
            ws_conn_t conn;
            conn.fd = lws_get_socket_fd(wsi);
            memset(conn.dev_sn, 0, sizeof(conn.dev_sn));
            conn.user_data = client;
            
            if (server && server->on_connect) {
                server->on_connect(&conn, server->user_data);
            }
            
            LOG_INFO("WS_SERVER", "New connection from fd=%d", conn.fd);
            break;
        }
        
        case LWS_CALLBACK_CLOSED: {
            ws_conn_t conn;
            conn.fd = lws_get_socket_fd(wsi);
            strncpy(conn.dev_sn, client->dev_sn, sizeof(conn.dev_sn) - 1);
            conn.user_data = client;
            
            if (server && server->on_disconnect) {
                server->on_disconnect(&conn, server->user_data);
            }
            
            pthread_mutex_lock(&conn_mutex);
            HASH_DEL(server->connections, client);
            pthread_mutex_unlock(&conn_mutex);
            
            if (client->dev_sn[0] != '\0') {
                LOG_INFO("WS_SERVER", "Connection closed: %s", client->dev_sn);
            } else {
                LOG_INFO("WS_SERVER", "Connection closed: fd=%d", conn.fd);
            }
            break;
        }
        
        case LWS_CALLBACK_RECEIVE: {
            client->last_activity = time(NULL);
            
            if (!server || !server->on_message) {
                break;
            }
            
            char buffer[MAX_PAYLOAD + 1];
            memset(buffer, 0, sizeof(buffer));
            len = len > MAX_PAYLOAD ? MAX_PAYLOAD : len;
            memcpy(buffer, in, len);
            
            const char* msg_type = "data";
            const char* msg_id = NULL;
            const char* payload = buffer;
            
            cJSON* root = cJSON_Parse(buffer);
            if (root) {
                cJSON* type_item = cJSON_GetObjectItem(root, "type");
                if (type_item && cJSON_IsString(type_item)) {
                    msg_type = type_item->valuestring;
                }
                
                cJSON* msgid_item = cJSON_GetObjectItem(root, "msgid");
                if (msgid_item && cJSON_IsString(msgid_item)) {
                    msg_id = msgid_item->valuestring;
                }
                
                cJSON* payload_item = cJSON_GetObjectItem(root, "payload");
                if (payload_item) {
                    char* payload_str = cJSON_PrintUnformatted(payload_item);
                    if (payload_str) {
                        payload = payload_str;
                    }
                }
                
                ws_conn_t conn;
                conn.fd = lws_get_socket_fd(wsi);
                strncpy(conn.dev_sn, client->dev_sn, sizeof(conn.dev_sn) - 1);
                conn.user_data = client;
                
                server->on_message(&conn, msg_type, msg_id, payload, server->user_data);
                
                if (payload != buffer && payload != NULL) {
                    free((void*)payload);
                }
                
                cJSON_Delete(root);
            } else {
                ws_conn_t conn;
                conn.fd = lws_get_socket_fd(wsi);
                strncpy(conn.dev_sn, client->dev_sn, sizeof(conn.dev_sn) - 1);
                conn.user_data = client;
                
                server->on_message(&conn, msg_type, msg_id, payload, server->user_data);
            }
            
            break;
        }
        
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            break;
        }
        
        default:
            break;
    }
    
    return 0;
}