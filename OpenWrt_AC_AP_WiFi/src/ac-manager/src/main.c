#include "../include/device_manager.h"
#include "../include/config_manager.h"
#include "../include/db_interface.h"
#include "../include/redis_interface.h"
#include "../include/ws_server.h"
#include "../include/msg_bus.h"
#include "../include/auth.h"
#include "../../common/include/log.h"
#include "../../common/include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define DEFAULT_CONFIG_FILE "/etc/ac-manager/ac-manager.conf"

static int g_running = 1;

static device_manager_t g_device_manager;
static config_manager_t g_config_manager;
static db_conn_t g_db_conn;
static redis_conn_t g_redis_conn;
static ws_server_t g_ws_server;
static msg_bus_t g_msg_bus;
static auth_manager_t g_auth_manager;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        LOG_INFO("MAIN", "Received signal %d, shutting down...", sig);
        g_running = 0;
        ws_server_stop(&g_ws_server);
    }
}

static void on_ws_connect(ws_conn_t* conn, void* user_data) {
    LOG_INFO("MAIN", "Device connected: fd=%d", conn->fd);
}

static void on_ws_disconnect(ws_conn_t* conn, void* user_data) {
    LOG_INFO("MAIN", "Device disconnected: %s", conn->dev_sn);
    
    device_manager_update_status(&g_device_manager, conn->dev_sn, DEV_OFFLINE);
    db_update_device_status(&g_db_conn, conn->dev_sn, DEV_OFFLINE);
}

static void on_ws_message(ws_conn_t* conn, const char* msg_type,
                          const char* msg_id, const char* payload, void* user_data) {
    LOG_DEBUG("MAIN", "Received message from %s: type=%s", conn->dev_sn, msg_type);
    
    if (strcmp(msg_type, "register") == 0) {
        device_info_t info;
        memset(&info, 0, sizeof(info));
        strncpy(info.dev_sn, conn->dev_sn, sizeof(info.dev_sn) - 1);
        info.status = DEV_ONLINE;
        info.register_time = time(NULL);
        info.last_heartbeat = time(NULL);
        
        device_manager_add(&g_device_manager, &info);
        db_save_device(&g_db_conn, &info);
        
        auth_log_action(&g_auth_manager, conn->dev_sn, NULL, "device_register", conn->dev_sn, "success", NULL);
    } else if (strcmp(msg_type, "heartbeat") == 0) {
        device_manager_update_heartbeat(&g_device_manager, conn->dev_sn, time(NULL));
        db_update_device_heartbeat(&g_db_conn, conn->dev_sn, time(NULL));
    } else if (strcmp(msg_type, "config_request") == 0) {
        auth_result_t auth_result = auth_check_permission(&g_auth_manager, conn->dev_sn, "config_view", NULL, NULL);
        if (auth_result != AUTH_RESULT_SUCCESS) {
            LOG_WARN("MAIN", "Permission denied for device %s to view config", conn->dev_sn);
            auth_log_action(&g_auth_manager, conn->dev_sn, NULL, "config_view", NULL, "denied", NULL);
            return;
        }
        
        auth_log_action(&g_auth_manager, conn->dev_sn, NULL, "config_view", NULL, "success", NULL);
    }
    
    msg_bus_publish(&g_msg_bus, msg_type, msg_id ? msg_id : "", payload);
}

static int init_modules(const char* config_file) {
    config_t* config = config_create(config_file);
    if (!config) {
        LOG_ERROR("MAIN", "Failed to create config");
        return -1;
    }
    
    if (config_load(config) != 0) {
        LOG_WARN("MAIN", "Config file not found, using defaults");
    }
    
    log_config_t log_config;
    memset(&log_config, 0, sizeof(log_config));
    log_config.level = (log_level_t)config_get_int(config, "log.level", LOG_LEVEL_INFO);
    snprintf(log_config.filename, sizeof(log_config.filename), "%s", config_get(config, "log.filename", "/var/log/ac-manager.log"));
    log_config.max_size = config_get_int(config, "log.max_size", 10 * 1024 * 1024);
    log_config.rotate_count = config_get_int(config, "log.rotate_count", 5);
    
    if (log_init(&log_config) != 0) {
        LOG_ERROR("MAIN", "Failed to init log system");
        config_destroy(config);
        return -1;
    }
    
    LOG_INFO("MAIN", "Starting AC Manager...");
    
    if (device_manager_init(&g_device_manager) != 0) {
        LOG_ERROR("MAIN", "Failed to init device manager");
        config_destroy(config);
        return -1;
    }
    
    if (config_manager_init(&g_config_manager) != 0) {
        LOG_ERROR("MAIN", "Failed to init config manager");
        device_manager_destroy(&g_device_manager);
        config_destroy(config);
        return -1;
    }
    
    if (auth_manager_init(&g_auth_manager) != 0) {
        LOG_ERROR("MAIN", "Failed to init auth manager");
        config_manager_destroy(&g_config_manager);
        device_manager_destroy(&g_device_manager);
        config_destroy(config);
        return -1;
    }
    
    if (msg_bus_init(&g_msg_bus) != 0) {
        LOG_ERROR("MAIN", "Failed to init msg bus");
        auth_manager_destroy(&g_auth_manager);
        config_manager_destroy(&g_config_manager);
        device_manager_destroy(&g_device_manager);
        config_destroy(config);
        return -1;
    }
    
    const char* db_type = config_get(config, "database.type", "sqlite");
    const char* db_name = config_get(config, "database.name", "/var/lib/ac-manager/ac-manager.db");
    
    if (strcmp(db_type, "mysql") == 0) {
        if (db_init(&g_db_conn, DB_TYPE_MYSQL,
                    config_get(config, "database.host", "localhost"),
                    config_get_int(config, "database.port", 3306),
                    db_name,
                    config_get(config, "database.user", "root"),
                    config_get(config, "database.password", "")) != 0) {
            LOG_ERROR("MAIN", "Failed to init MySQL connection");
            msg_bus_destroy(&g_msg_bus);
            auth_manager_destroy(&g_auth_manager);
            config_manager_destroy(&g_config_manager);
            device_manager_destroy(&g_device_manager);
            config_destroy(config);
            return -1;
        }
    } else {
        if (db_init(&g_db_conn, DB_TYPE_SQLITE, NULL, 0, db_name, NULL, NULL) != 0) {
            LOG_ERROR("MAIN", "Failed to init SQLite connection");
            msg_bus_destroy(&g_msg_bus);
            auth_manager_destroy(&g_auth_manager);
            config_manager_destroy(&g_config_manager);
            device_manager_destroy(&g_device_manager);
            config_destroy(config);
            return -1;
        }
    }
    
#ifdef USE_REDIS
    if (redis_init(&g_redis_conn,
                   config_get(config, "redis.host", "localhost"),
                   config_get_int(config, "redis.port", 6379),
                   config_get(config, "redis.password", "")) != 0) {
        LOG_WARN("MAIN", "Failed to init Redis connection, running without Redis support");
    } else {
        LOG_INFO("MAIN", "Redis initialized successfully");
    }
#endif
    
    ws_server_config_t ws_config;
    memset(&ws_config, 0, sizeof(ws_server_config_t));
    ws_config.port = config_get_int(config, "websocket.port", 8080);
    ws_config.cert_file = config_get(config, "websocket.cert_file", NULL);
    ws_config.key_file = config_get(config, "websocket.key_file", NULL);
    ws_config.ca_file = config_get(config, "websocket.ca_file", NULL);
    ws_config.max_connections = config_get_int(config, "websocket.max_connections", 1000);
    ws_config.heartbeat_interval = config_get_int(config, "websocket.heartbeat_interval", 30);
    ws_config.idle_timeout = config_get_int(config, "websocket.idle_timeout", 300);
    
    if (ws_server_init(&g_ws_server, &ws_config) != 0) {
        LOG_ERROR("MAIN", "Failed to init WebSocket server");
        db_close(&g_db_conn);
        msg_bus_destroy(&g_msg_bus);
        auth_manager_destroy(&g_auth_manager);
        config_manager_destroy(&g_config_manager);
        device_manager_destroy(&g_device_manager);
        config_destroy(config);
        return -1;
    }
    
    g_ws_server.on_connect = on_ws_connect;
    g_ws_server.on_disconnect = on_ws_disconnect;
    g_ws_server.on_message = on_ws_message;
    g_ws_server.user_data = NULL;
    
    config_destroy(config);
    
    LOG_INFO("MAIN", "All modules initialized successfully");
    return 0;
}

static void cleanup_modules(void) {
    LOG_INFO("MAIN", "Cleaning up modules...");
    
    ws_server_destroy(&g_ws_server);
    
#ifdef USE_REDIS
    redis_close(&g_redis_conn);
#endif
    
    db_close(&g_db_conn);
    
    msg_bus_destroy(&g_msg_bus);
    
    auth_manager_destroy(&g_auth_manager);
    
    config_manager_destroy(&g_config_manager);
    
    device_manager_destroy(&g_device_manager);
    
    log_deinit();
    
    LOG_INFO("MAIN", "Cleanup complete");
}

static void heartbeat_timeout_callback(const char* dev_sn, void* user_data) {
    LOG_WARN("MAIN", "Device timeout detected: %s", dev_sn);
    device_manager_update_status(&g_device_manager, dev_sn, DEV_OFFLINE);
    db_update_device_status(&g_db_conn, dev_sn, DEV_OFFLINE);
}

static void* heartbeat_check_thread(void* arg) {
    while (g_running) {
        device_manager_check_timeout(&g_device_manager, 60, heartbeat_timeout_callback, NULL);
        sleep(30);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    const char* config_file = DEFAULT_CONFIG_FILE;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_file = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-c config_file]\n", argv[0]);
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (init_modules(config_file) != 0) {
        LOG_FATAL("MAIN", "Failed to initialize modules");
        return 1;
    }
    
    pthread_t hb_thread;
    pthread_create(&hb_thread, NULL, heartbeat_check_thread, NULL);
    
    LOG_INFO("MAIN", "AC Manager started, listening on port %d", g_ws_server.config.port);
    
    ws_server_start(&g_ws_server);
    
    pthread_join(hb_thread, NULL);
    
    cleanup_modules();
    
    LOG_INFO("MAIN", "AC Manager stopped");
    return 0;
}