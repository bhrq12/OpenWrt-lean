#include "redis_interface.h"
#include "common/include/log.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef USE_REDIS
#include <hiredis/hiredis.h>
#endif

int redis_init(redis_conn_t* conn, const char* host, int port, const char* password) {
    if (!conn) {
        return -1;
    }
    
    memset(conn, 0, sizeof(redis_conn_t));
    
#ifdef USE_REDIS
    redisContext* ctx = redisConnect(host, port);
    if (!ctx || ctx->err) {
        if (ctx) {
            LOG_ERROR("REDIS", "Redis connect failed: %s", ctx->errstr);
            redisFree(ctx);
        } else {
            LOG_ERROR("REDIS", "Redis connect failed: cannot allocate context");
        }
        return -1;
    }
    
    if (password && strlen(password) > 0) {
        const char* argv[] = {"AUTH", password};
        int argc = 2;
        redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG_ERROR("REDIS", "Redis auth failed: %s", reply ? reply->str : "unknown");
            freeReplyObject(reply);
            redisFree(ctx);
            return -1;
        }
        freeReplyObject(reply);
    }
    
    conn->handle = ctx;
    conn->connected = 1;
    
    LOG_INFO("REDIS", "Redis initialized: %s:%d", host, port);
    return 0;
#else
    LOG_ERROR("REDIS", "Redis support not compiled");
    return -1;
#endif
}

#ifdef USE_REDIS
static void* redis_subscribe_thread(void* arg) {
    redis_conn_t* conn = (redis_conn_t*)arg;
    redisContext* ctx = (redisContext*)conn->sub_handle;
    
    while (conn->sub_running && ctx && ctx->err == 0) {
        redisReply* reply = redisGetReply(ctx);
        if (!reply) {
            LOG_ERROR("REDIS", "Failed to get reply in subscribe thread");
            break;
        }
        
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
            if (strcmp(reply->element[0]->str, "message") == 0) {
                const char* channel = reply->element[1]->str;
                const char* message = reply->element[2]->str;
                
                if (conn->callback) {
                    conn->callback(channel, message, conn->callback_data);
                }
                
                LOG_DEBUG("REDIS", "Received message on channel %s: %s", channel, message);
            }
        }
        
        freeReplyObject(reply);
    }
    
    LOG_INFO("REDIS", "Subscribe thread stopped");
    return NULL;
}
#endif

void redis_close(redis_conn_t* conn) {
    if (!conn) {
        return;
    }
    
#ifdef USE_REDIS
    if (conn->sub_running) {
        conn->sub_running = 0;
        
        if (conn->sub_handle) {
            redisContext* ctx = (redisContext*)conn->sub_handle;
            const char* argv[] = {"UNSUBSCRIBE"};
            int argc = 1;
            redisCommandArgv(ctx, argc, argv, NULL);
            redisFree(ctx);
            conn->sub_handle = NULL;
        }
        
        if (pthread_join(conn->sub_thread, NULL) == 0) {
            LOG_INFO("REDIS", "Subscribe thread joined");
        }
    }
    
    if (conn->handle) {
        redisFree((redisContext*)conn->handle);
        conn->handle = NULL;
    }
    
    conn->connected = 0;
    
    LOG_INFO("REDIS", "Redis connection closed");
#endif
}

int redis_set_device_online(redis_conn_t* conn, const char* dev_sn, device_status_t status, time_t ttl) {
    if (!conn || !conn->handle || !dev_sn) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    char key[128];
    snprintf(key, sizeof(key), "device:%s:status", dev_sn);
    
    char status_str[16];
    snprintf(status_str, sizeof(status_str), "%d", status);
    
    redisReply* reply;
    if (ttl > 0) {
        char ttl_str[16];
        snprintf(ttl_str, sizeof(ttl_str), "%ld", (long)ttl);
        
        const char* argv[] = {"SET", key, status_str, "EX", ttl_str};
        int argc = 5;
        reply = redisCommandArgv(ctx, argc, argv, NULL);
    } else {
        const char* argv[] = {"SET", key, status_str};
        int argc = 3;
        reply = redisCommandArgv(ctx, argc, argv, NULL);
    }
    
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to set device status: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_get_device_status(redis_conn_t* conn, const char* dev_sn) {
    if (!conn || !conn->handle || !dev_sn) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    char key[128];
    snprintf(key, sizeof(key), "device:%s:status", dev_sn);
    
    const char* argv[] = {"GET", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply) {
        LOG_ERROR("REDIS", "Failed to get device status");
        return -1;
    }
    
    int status = -1;
    if (reply->type == REDIS_REPLY_STRING) {
        status = atoi(reply->str);
    }
    
    freeReplyObject(reply);
    return status;
#else
    return -1;
#endif
}

int redis_set_config_version(redis_conn_t* conn, const char* config_id, int version) {
    if (!conn || !conn->handle || !config_id) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    char key[128];
    snprintf(key, sizeof(key), "config:%s:version", config_id);
    
    char version_str[16];
    snprintf(version_str, sizeof(version_str), "%d", version);
    
    const char* argv[] = {"SET", key, version_str};
    int argc = 3;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to set config version: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_get_config_version(redis_conn_t* conn, const char* config_id) {
    if (!conn || !conn->handle || !config_id) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    char key[128];
    snprintf(key, sizeof(key), "config:%s:version", config_id);
    
    const char* argv[] = {"GET", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply) {
        LOG_ERROR("REDIS", "Failed to get config version");
        return -1;
    }
    
    int version = -1;
    if (reply->type == REDIS_REPLY_STRING) {
        version = atoi(reply->str);
    }
    
    freeReplyObject(reply);
    return version;
#else
    return -1;
#endif
}

int redis_publish(redis_conn_t* conn, const char* channel, const char* message) {
    if (!conn || !conn->handle || !channel || !message) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    const char* argv[] = {"PUBLISH", channel, message};
    int argc = 3;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to publish: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_subscribe(redis_conn_t* conn, const char* channel, redis_subscribe_callback_t callback, void* user_data) {
    if (!conn || !channel || !callback) {
        return -1;
    }
    
#ifdef USE_REDIS
    if (conn->sub_running) {
        LOG_WARN("REDIS", "Already subscribed");
        return -1;
    }
    
    const char* host = "127.0.0.1";
    int port = 6379;
    
    if (conn->handle) {
        redisContext* ctx = (redisContext*)conn->handle;
        host = ctx->tcp.host;
        port = ctx->tcp.port;
    }
    
    redisContext* sub_ctx = redisConnect(host, port);
    if (!sub_ctx || sub_ctx->err) {
        if (sub_ctx) {
            LOG_ERROR("REDIS", "Subscribe connect failed: %s", sub_ctx->errstr);
            redisFree(sub_ctx);
        } else {
            LOG_ERROR("REDIS", "Subscribe connect failed: cannot allocate context");
        }
        return -1;
    }
    
    conn->sub_handle = sub_ctx;
    conn->callback = callback;
    conn->callback_data = user_data;
    conn->sub_running = 1;
    
    const char* argv[] = {"SUBSCRIBE", channel};
    int argc = 2;
    redisReply* reply = redisCommandArgv(sub_ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Subscribe failed: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        redisFree(sub_ctx);
        conn->sub_handle = NULL;
        conn->sub_running = 0;
        return -1;
    }
    
    freeReplyObject(reply);
    
    int rc = pthread_create(&conn->sub_thread, NULL, redis_subscribe_thread, conn);
    if (rc != 0) {
        LOG_ERROR("REDIS", "Failed to create subscribe thread");
        const char* argv_unsub[] = {"UNSUBSCRIBE"};
        int argc_unsub = 1;
        redisCommandArgv(sub_ctx, argc_unsub, argv_unsub, NULL);
        redisFree(sub_ctx);
        conn->sub_handle = NULL;
        conn->sub_running = 0;
        return -1;
    }
    
    LOG_INFO("REDIS", "Subscribed to channel: %s", channel);
    return 0;
#else
    LOG_ERROR("REDIS", "Redis support not compiled");
    return -1;
#endif
}

int redis_set(redis_conn_t* conn, const char* key, const char* value, time_t ttl) {
    if (!conn || !conn->handle || !key || !value) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    redisReply* reply;
    if (ttl > 0) {
        char ttl_str[16];
        snprintf(ttl_str, sizeof(ttl_str), "%ld", (long)ttl);
        
        const char* argv[] = {"SET", key, value, "EX", ttl_str};
        int argc = 5;
        reply = redisCommandArgv(ctx, argc, argv, NULL);
    } else {
        const char* argv[] = {"SET", key, value};
        int argc = 3;
        reply = redisCommandArgv(ctx, argc, argv, NULL);
    }
    
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to set key: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_get(redis_conn_t* conn, const char* key, char* value, int value_len) {
    if (!conn || !conn->handle || !key || !value) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    const char* argv[] = {"GET", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply) {
        LOG_ERROR("REDIS", "Failed to get key");
        return -1;
    }
    
    int ret = -1;
    if (reply->type == REDIS_REPLY_STRING) {
        strncpy(value, reply->str, value_len - 1);
        value[value_len - 1] = '\0';
        ret = 0;
    }
    
    freeReplyObject(reply);
    return ret;
#else
    return -1;
#endif
}

int redis_del(redis_conn_t* conn, const char* key) {
    if (!conn || !conn->handle || !key) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    const char* argv[] = {"DEL", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to delete key: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_incr(redis_conn_t* conn, const char* key, int* result) {
    if (!conn || !conn->handle || !key || !result) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    const char* argv[] = {"INCR", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to incr key: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    if (reply->type == REDIS_REPLY_INTEGER) {
        *result = (int)reply->integer;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}

int redis_decr(redis_conn_t* conn, const char* key, int* result) {
    if (!conn || !conn->handle || !key || !result) {
        return -1;
    }
    
#ifdef USE_REDIS
    redisContext* ctx = (redisContext*)conn->handle;
    
    const char* argv[] = {"DECR", key};
    int argc = 2;
    redisReply* reply = redisCommandArgv(ctx, argc, argv, NULL);
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR("REDIS", "Failed to decr key: %s", reply ? reply->str : "unknown");
        freeReplyObject(reply);
        return -1;
    }
    
    if (reply->type == REDIS_REPLY_INTEGER) {
        *result = (int)reply->integer;
    }
    
    freeReplyObject(reply);
    return 0;
#else
    return -1;
#endif
}