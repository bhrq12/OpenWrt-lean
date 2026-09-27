#include "../include/ws_client.h"
#include "../include/config_adapter.h"
#include "../include/sys_bus.h"
#include "../../common/include/log.h"
#include "../../common/include/config.h"
#include "../../common/include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

static int g_running = 1;
static ws_client_t g_ws_client;
static config_adapter_t g_config_adapter;
static sys_bus_t g_sys_bus;
static config_t* g_config = NULL;

static void signal_handler(int sig) {
    g_running = 0;
    LOG_INFO("AP", "Received signal %d, stopping...", sig);
}

static void on_ws_message(ws_client_t* client, const char* msg_type,
                          const char* msg_id, const char* payload, void* user_data) {
    (void)client; (void)user_data;
    const char* type = msg_type ? msg_type : "";
    LOG_INFO("AP", "Received message: type=%s, msgid=%s", type, msg_id ? msg_id : "NULL");

    if (strcmp(type, "config_sync") == 0 && payload) {
        LOG_INFO("AP", "Processing config sync");
        adapter_result_t result = config_adapter_apply(&g_config_adapter, payload);
        if (result == ADAPTER_RESULT_SUCCESS) {
            LOG_INFO("AP", "Config applied successfully");
        } else {
            LOG_ERROR("AP", "Config apply failed: %d", result);
        }
    } else if (strcmp(type, "config_apply") == 0) {
        LOG_INFO("AP", "Processing config apply");
        sys_bus_reload_wifi(&g_sys_bus);
    }
}

static void on_ws_connect(ws_client_t* client, void* user_data) {
    (void)client; (void)user_data;
    LOG_INFO("AP", "Connected to AC controller");
}

static void on_ws_disconnect(ws_client_t* client, void* user_data) {
    (void)client; (void)user_data;
    LOG_WARN("AP", "Disconnected from AC controller");
}

static void send_heartbeat(void) {
    system_info_t sys_info;
    if (sys_bus_get_system_info(&g_sys_bus, &sys_info) == 0) {
        char payload[512];
        snprintf(payload, sizeof(payload),
                 "{\"dev_sn\":\"%s\",\"cpu_usage\":%d,\"mem_usage\":%d,\"client_count\":0}",
                 "AP_LOCAL", sys_info.cpu_usage, sys_info.mem_usage);
        ws_client_send(&g_ws_client, "heartbeat", NULL, payload);
    }
}

int main(int argc, char* argv[]) {
    char* config_file = "/etc/ap-manager/ap-manager.conf";
    int opt;
    
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-c config_file]\n", argv[0]);
                return 1;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    g_config = config_create(config_file);
    if (!g_config || config_load(g_config) != 0) {
        fprintf(stderr, "Failed to load config file: %s\n", config_file);
        return 1;
    }
    
    log_config_t log_config;
    memset(&log_config, 0, sizeof(log_config));
    strncpy(log_config.filename, config_get(g_config, "logging.file", "/var/log/ap-manager.log"), sizeof(log_config.filename) - 1);
    log_config.level = LOG_LEVEL_INFO;
    log_config.max_size = config_get_int(g_config, "logging.max_size", 10485760);
    log_config.rotate_count = config_get_int(g_config, "logging.rotate_count", 5);
    
    if (log_init(&log_config) != 0) {
        fprintf(stderr, "Failed to init log system\n");
        return 1;
    }
    
    LOG_INFO("AP", "Starting AP Manager v1.0.0");
    
    if (sys_bus_init(&g_sys_bus) != 0) {
        LOG_ERROR("AP", "Failed to init system bus");
        return 1;
    }
    
    if (config_adapter_init(&g_config_adapter) != 0) {
        LOG_ERROR("AP", "Failed to init config adapter");
        return 1;
    }
    
    ws_client_config_t ws_config;
    memset(&ws_config, 0, sizeof(ws_config));
    ws_config.ac_url = config_get(g_config, "client.ac_url", "wss://localhost:8080/ws");
    ws_config.cert_file = config_get(g_config, "client.cert_file", "/etc/ap-manager/cert.pem");
    ws_config.key_file = config_get(g_config, "client.key_file", "/etc/ap-manager/key.pem");
    ws_config.ca_file = config_get(g_config, "client.ca_file", "/etc/ap-manager/ca.pem");
    ws_config.reconnect_interval = config_get_int(g_config, "client.reconnect_interval", 10);
    ws_config.heartbeat_interval = config_get_int(g_config, "heartbeat.interval", 30);
    
    if (ws_client_init(&g_ws_client, &ws_config) != 0) {
        LOG_ERROR("AP", "Failed to init WebSocket client");
        return 1;
    }
    
    ws_client_set_callbacks(&g_ws_client, on_ws_connect, on_ws_disconnect, on_ws_message);
    
    LOG_INFO("AP", "Connecting to AC controller: %s", ws_config.ac_url);
    
    if (ws_client_start(&g_ws_client) != 0) {
        LOG_ERROR("AP", "Failed to start WebSocket client");
        return 1;
    }
    
    LOG_INFO("AP", "AP Manager started successfully");
    
    while (g_running) {
        sleep(1);
        static time_t last_heartbeat = 0;
        time_t now = time(NULL);
        if (now - last_heartbeat >= g_ws_client.config.heartbeat_interval) {
            last_heartbeat = now;
            send_heartbeat();
        }
    }
    
    LOG_INFO("AP", "Stopping AP Manager...");
    
    ws_client_stop(&g_ws_client);
    ws_client_destroy(&g_ws_client);
    config_adapter_destroy(&g_config_adapter);
    sys_bus_close(&g_sys_bus);
    log_deinit();
    config_destroy(g_config);
    
    LOG_INFO("AP", "AP Manager stopped");
    
    return 0;
}