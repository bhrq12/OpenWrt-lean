#include "sys_bus.h"
#include "../common/include/log.h"
#include <libubus.h>
#include <libuci.h>
#include <string.h>
#include <stdlib.h>

int sys_bus_init(sys_bus_t* bus) {
    if (!bus) {
        return -1;
    }
    
    memset(bus, 0, sizeof(sys_bus_t));
    
    struct ubus_context* ctx = ubus_connect(NULL);
    if (!ctx) {
        LOG_ERROR("SYS_BUS", "Failed to connect to ubus");
        return -1;
    }
    
    bus->ctx = ctx;
    bus->connected = 1;
    
    LOG_INFO("SYS_BUS", "System bus initialized");
    return 0;
}

void sys_bus_close(sys_bus_t* bus) {
    if (!bus || !bus->ctx) {
        return;
    }
    
    ubus_free((struct ubus_context*)bus->ctx);
    bus->ctx = NULL;
    bus->connected = 0;
    
    LOG_INFO("SYS_BUS", "System bus closed");
}

int sys_bus_get_radio_info(sys_bus_t* bus, radio_info_t** radios, int* count) {
    *radios = NULL;
    *count = 0;
    
    if (!bus || !bus->ctx) {
        return -1;
    }
    
    struct ubus_context* ctx = (struct ubus_context*)bus->ctx;
    struct ubus_object_data* objects;
    int n;
    
    if (ubus_lookup(ctx, "network.wireless", &objects, &n) != 0) {
        return 0;
    }
    
    *count = n;
    *radios = (radio_info_t*)malloc(n * sizeof(radio_info_t));
    if (!*radios) {
        ubus_free_object_data(objects, n);
        return -1;
    }
    
    for (int i = 0; i < n; i++) {
        memset(&(*radios)[i], 0, sizeof(radio_info_t));
        strncpy((*radios)[i].name, objects[i].name, sizeof((*radios)[i].name) - 1);
    }
    
    ubus_free_object_data(objects, n);
    return 0;
}

int sys_bus_set_radio_config(sys_bus_t* bus, const char* radio_name, int channel, int txpower) {
    if (!bus || !bus->ctx || !radio_name) {
        return -1;
    }
    
    struct uci_context* uci = uci_alloc_context();
    if (!uci) {
        return -1;
    }
    
    char section[64];
    snprintf(section, sizeof(section), "radio.%s", radio_name + strlen("radio."));
    
    if (uci_set(uci, "wireless", section, "channel", NULL, NULL, "%d", channel) != UCI_OK) {
        LOG_ERROR("SYS_BUS", "Failed to set radio channel");
        uci_free_context(uci);
        return -1;
    }
    
    if (txpower > 0) {
        if (uci_set(uci, "wireless", section, "txpower", NULL, NULL, "%d", txpower) != UCI_OK) {
            LOG_ERROR("SYS_BUS", "Failed to set radio txpower");
            uci_free_context(uci);
            return -1;
        }
    }
    
    uci_commit(uci, "wireless", false);
    uci_free_context(uci);
    
    LOG_INFO("SYS_BUS", "Radio %s config updated: channel=%d, txpower=%d", radio_name, channel, txpower);
    return 0;
}

int sys_bus_get_wifi_iface_info(sys_bus_t* bus, wifi_iface_info_t** ifaces, int* count) {
    *ifaces = NULL;
    *count = 0;
    
    if (!bus || !bus->ctx) {
        return -1;
    }
    
    struct uci_context* uci = uci_alloc_context();
    if (!uci) {
        return -1;
    }
    
    struct uci_package* pkg;
    if (uci_load(uci, "wireless", &pkg) != UCI_OK) {
        uci_free_context(uci);
        return 0;
    }
    
    struct uci_section* s;
    int n = 0;
    
    uci_foreach_section(pkg, s) {
        if (strcmp(uci_section_type(s), "wifi-iface") == 0) {
            n++;
        }
    }
    
    *count = n;
    *ifaces = (wifi_iface_info_t*)malloc(n * sizeof(wifi_iface_info_t));
    if (!*ifaces) {
        uci_unload(uci, pkg);
        uci_free_context(uci);
        return -1;
    }
    
    int idx = 0;
    uci_foreach_section(pkg, s) {
        if (strcmp(uci_section_type(s), "wifi-iface") == 0) {
            memset(&(*ifaces)[idx], 0, sizeof(wifi_iface_info_t));
            strncpy((*ifaces)[idx].name, uci_section_name(s), sizeof((*ifaces)[idx].name) - 1);
            
            struct uci_option* o = uci_find_option(s, "ssid");
            if (o) strncpy((*ifaces)[idx].ssid, o->v.string, sizeof((*ifaces)[idx].ssid) - 1);
            
            o = uci_find_option(s, "encryption");
            if (o) strncpy((*ifaces)[idx].encryption, o->v.string, sizeof((*ifaces)[idx].encryption) - 1);
            
            idx++;
        }
    }
    
    uci_unload(uci, pkg);
    uci_free_context(uci);
    return 0;
}

int sys_bus_set_wifi_iface_config(sys_bus_t* bus, const char* iface_name, const char* ssid,
                                   const char* encryption, const char* key) {
    if (!bus || !bus->ctx || !iface_name || !ssid) {
        return -1;
    }
    
    struct uci_context* uci = uci_alloc_context();
    if (!uci) {
        return -1;
    }
    
    if (uci_set(uci, "wireless", iface_name, "ssid", NULL, NULL, "%s", ssid) != UCI_OK) {
        LOG_ERROR("SYS_BUS", "Failed to set SSID");
        uci_free_context(uci);
        return -1;
    }
    
    if (encryption && strlen(encryption) > 0) {
        if (uci_set(uci, "wireless", iface_name, "encryption", NULL, NULL, "%s", encryption) != UCI_OK) {
            LOG_ERROR("SYS_BUS", "Failed to set encryption");
            uci_free_context(uci);
            return -1;
        }
    }
    
    if (key && strlen(key) > 0) {
        if (uci_set(uci, "wireless", iface_name, "key", NULL, NULL, "%s", key) != UCI_OK) {
            LOG_ERROR("SYS_BUS", "Failed to set key");
            uci_free_context(uci);
            return -1;
        }
    }
    
    uci_commit(uci, "wireless", false);
    uci_free_context(uci);
    
    LOG_INFO("SYS_BUS", "Interface %s config updated: SSID=%s", iface_name, ssid);
    return 0;
}

int sys_bus_get_system_info(sys_bus_t* bus, system_info_t* info) {
    if (!bus || !bus->ctx || !info) {
        return -1;
    }
    
    memset(info, 0, sizeof(system_info_t));
    
    FILE* f = fopen("/proc/uptime", "r");
    if (f) {
        double uptime;
        fscanf(f, "%lf", &uptime);
        info->uptime = (int)uptime;
        fclose(f);
    }
    
    f = fopen("/proc/stat", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "cpu ", 4) == 0) {
                int user, nice, sys, idle;
                sscanf(line, "cpu %d %d %d %d", &user, &nice, &sys, &idle);
                int total = user + nice + sys + idle;
                info->cpu_usage = total > 0 ? ((user + sys) * 100) / total : 0;
                break;
            }
        }
        fclose(f);
    }
    
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        int total = 0, free_mem = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line, "MemTotal: %d", &total);
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line, "MemFree: %d", &free_mem);
            }
        }
        info->mem_usage = total > 0 ? ((total - free_mem) * 100) / total : 0;
        fclose(f);
    }
    
    return 0;
}

int sys_bus_reload_wifi(sys_bus_t* bus) {
    if (!bus || !bus->ctx) {
        return -1;
    }
    
    struct ubus_context* ctx = (struct ubus_context*)bus->ctx;
    uint32_t id;
    
    if (ubus_lookup_id(ctx, "network.wireless", &id) != 0) {
        LOG_ERROR("SYS_BUS", "Failed to lookup wireless object");
        return -1;
    }
    
    struct ubus_request_data req;
    memset(&req, 0, sizeof(req));
    
    if (ubus_invoke(ctx, id, "reload", NULL, &req, NULL, 3000) != 0) {
        LOG_ERROR("SYS_BUS", "Failed to reload wireless");
        return -1;
    }
    
    LOG_INFO("SYS_BUS", "WiFi configuration reloaded");
    return 0;
}

int sys_bus_get_client_count(sys_bus_t* bus, const char* iface_name, int* count) {
    *count = 0;
    
    if (!bus || !bus->ctx || !iface_name) {
        return -1;
    }
    
    struct ubus_context* ctx = (struct ubus_context*)bus->ctx;
    uint32_t id;
    
    if (ubus_lookup_id(ctx, "network.wireless", &id) != 0) {
        return 0;
    }
    
    *count = 0;
    return 0;
}