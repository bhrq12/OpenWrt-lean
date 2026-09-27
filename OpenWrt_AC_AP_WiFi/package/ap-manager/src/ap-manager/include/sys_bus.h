#ifndef SYS_BUS_H
#define SYS_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    void* ctx;
    int connected;
} sys_bus_t;

typedef struct {
    char name[64];
    char type[32];
    int channel;
    int width;
    int country[2];
    int txpower;
    int htmode[4];
} radio_info_t;

typedef struct {
    char name[64];
    char ssid[32];
    char encryption[16];
    char key[64];
    int hidden;
    int isolate;
} wifi_iface_info_t;

typedef struct {
    int cpu_usage;
    int mem_usage;
    int uptime;
} system_info_t;

int sys_bus_init(sys_bus_t* bus);

void sys_bus_close(sys_bus_t* bus);

int sys_bus_get_radio_info(sys_bus_t* bus, radio_info_t** radios, int* count);

int sys_bus_set_radio_config(sys_bus_t* bus, const char* radio_name, int channel, int txpower);

int sys_bus_get_wifi_iface_info(sys_bus_t* bus, wifi_iface_info_t** ifaces, int* count);

int sys_bus_set_wifi_iface_config(sys_bus_t* bus, const char* iface_name, const char* ssid,
                                   const char* encryption, const char* key);

int sys_bus_get_system_info(sys_bus_t* bus, system_info_t* info);

int sys_bus_reload_wifi(sys_bus_t* bus);

int sys_bus_get_client_count(sys_bus_t* bus, const char* iface_name, int* count);

#ifdef __cplusplus
}
#endif

#endif