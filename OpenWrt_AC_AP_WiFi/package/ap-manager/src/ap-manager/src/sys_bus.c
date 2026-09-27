#include "sys_bus.h"
#include "common/include/log.h"
#include <libubus.h>
#include <uci.h>
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

struct radio_lookup_data {
    radio_info_t* radios;
    int count;
    int max_count;
};

static void radio_lookup_cb(struct ubus_context* ctx, struct ubus_object_data* obj, void* priv) {
    (void)ctx;
    struct radio_lookup_data* data = (struct radio_lookup_data*)priv;
    if (!data || data->count >= data->max_count || !obj->path) {
        return;
    }

    strncpy(data->radios[data->count].name, obj->path,
            sizeof(data->radios[data->count].name) - 1);
    data->count++;
}

int sys_bus_get_radio_info(sys_bus_t* bus, radio_info_t** radios, int* count) {
    *radios = NULL;
    *count = 0;

    if (!bus || !bus->ctx) {
        return -1;
    }

    struct ubus_context* ctx = (struct ubus_context*)bus->ctx;

    radio_info_t tmp[16];
    memset(tmp, 0, sizeof(tmp));

    struct radio_lookup_data data = {
        .radios = tmp,
        .count = 0,
        .max_count = 16
    };

    if (ubus_lookup(ctx, "wireless", radio_lookup_cb, &data) != 0) {
        return 0;
    }

    if (data.count > 0) {
        *radios = (radio_info_t*)malloc(data.count * sizeof(radio_info_t));
        if (!*radios) {
            return -1;
        }
        memcpy(*radios, tmp, data.count * sizeof(radio_info_t));
    }

    *count = data.count;
    return 0;
}

static int uci_set_option(struct uci_context* ctx, const char* package,
                          const char* section, const char* option, const char* value) {
    struct uci_ptr ptr;
    char* str = NULL;
    int len = 0;

    if (!ctx || !package || !section) {
        return -1;
    }

    len = snprintf(NULL, 0, "%s.%s.%s", package, section, option ? option : "") + 1;
    str = (char*)malloc(len);
    if (!str) {
        return -1;
    }

    if (option) {
        snprintf(str, len, "%s.%s.%s", package, section, option);
    } else {
        snprintf(str, len, "%s.%s", package, section);
    }

    memset(&ptr, 0, sizeof(ptr));
    if (uci_lookup_ptr(ctx, &ptr, str, false) != UCI_OK) {
        free(str);
        return -1;
    }

    ptr.value = value;
    if (uci_set(ctx, &ptr) != UCI_OK) {
        free(str);
        return -1;
    }

    free(str);
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

    char channel_str[16];
    snprintf(channel_str, sizeof(channel_str), "%d", channel);

    if (uci_set_option(uci, "wireless", radio_name, "channel", channel_str) != 0) {
        LOG_ERROR("SYS_BUS", "Failed to set radio channel");
        uci_free_context(uci);
        return -1;
    }

    if (txpower > 0) {
        char txpower_str[16];
        snprintf(txpower_str, sizeof(txpower_str), "%d", txpower);
        if (uci_set_option(uci, "wireless", radio_name, "txpower", txpower_str) != 0) {
            LOG_ERROR("SYS_BUS", "Failed to set radio txpower");
            uci_free_context(uci);
            return -1;
        }
    }

    struct uci_package* pkg = uci_lookup_package(uci, "wireless");
    if (pkg) {
        uci_commit(uci, &pkg, false);
    }
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

    struct uci_package* pkg = NULL;
    if (uci_load(uci, "wireless", &pkg) != UCI_OK) {
        uci_free_context(uci);
        return 0;
    }

    struct uci_element* e;
    int n = 0;

    uci_foreach_element(&pkg->sections, e) {
        struct uci_section* s = uci_to_section(e);
        if (s->type && strcmp(s->type, "wifi-iface") == 0) {
            n++;
        }
    }

    if (n == 0) {
        uci_unload(uci, pkg);
        uci_free_context(uci);
        return 0;
    }

    *count = n;
    *ifaces = (wifi_iface_info_t*)malloc(n * sizeof(wifi_iface_info_t));
    if (!*ifaces) {
        uci_unload(uci, pkg);
        uci_free_context(uci);
        return -1;
    }

    int idx = 0;
    uci_foreach_element(&pkg->sections, e) {
        struct uci_section* s = uci_to_section(e);
        if (s->type && strcmp(s->type, "wifi-iface") == 0) {
            memset(&(*ifaces)[idx], 0, sizeof(wifi_iface_info_t));
            if (s->e.name) {
                strncpy((*ifaces)[idx].name, s->e.name, sizeof((*ifaces)[idx].name) - 1);
            }

            struct uci_option* o = uci_lookup_option(uci, s, "ssid");
            if (o && o->type == UCI_TYPE_STRING && o->v.string) {
                strncpy((*ifaces)[idx].ssid, o->v.string, sizeof((*ifaces)[idx].ssid) - 1);
            }

            o = uci_lookup_option(uci, s, "encryption");
            if (o && o->type == UCI_TYPE_STRING && o->v.string) {
                strncpy((*ifaces)[idx].encryption, o->v.string, sizeof((*ifaces)[idx].encryption) - 1);
            }

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

    if (uci_set_option(uci, "wireless", iface_name, "ssid", ssid) != 0) {
        LOG_ERROR("SYS_BUS", "Failed to set SSID");
        uci_free_context(uci);
        return -1;
    }

    if (encryption && strlen(encryption) > 0) {
        if (uci_set_option(uci, "wireless", iface_name, "encryption", encryption) != 0) {
            LOG_ERROR("SYS_BUS", "Failed to set encryption");
            uci_free_context(uci);
            return -1;
        }
    }

    if (key && strlen(key) > 0) {
        if (uci_set_option(uci, "wireless", iface_name, "key", key) != 0) {
            LOG_ERROR("SYS_BUS", "Failed to set key");
            uci_free_context(uci);
            return -1;
        }
    }

    struct uci_package* pkg = uci_lookup_package(uci, "wireless");
    if (pkg) {
        uci_commit(uci, &pkg, false);
    }
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
        if (fscanf(f, "%lf", &uptime) == 1) {
            info->uptime = (int)uptime;
        }
        fclose(f);
    }

    f = fopen("/proc/stat", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "cpu ", 4) == 0) {
                int user, nice, sys, idle;
                if (sscanf(line, "cpu %d %d %d %d", &user, &nice, &sys, &idle) == 4) {
                    int total = user + nice + sys + idle;
                    info->cpu_usage = total > 0 ? ((user + sys) * 100) / total : 0;
                }
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

static void ubus_invoke_cb(struct ubus_request* req, int type, struct blob_attr* msg) {
    (void)req; (void)type; (void)msg;
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

    if (ubus_invoke(ctx, id, "reload", NULL, ubus_invoke_cb, NULL, 3000) != 0) {
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
