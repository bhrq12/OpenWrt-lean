#ifndef MSG_BUS_H
#define MSG_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/include/common.h"
#include <pthread.h>

typedef int (*msg_handler_t)(const char* msg_type, const char* msg_id,
                             const char* payload, void* user_data);

typedef struct msg_handler_node {
    char msg_type[MAX_MSG_TYPE_LEN];
    msg_handler_t handler;
    void* user_data;
    struct msg_handler_node* next;
} msg_handler_node_t;

typedef struct {
    msg_handler_node_t* handlers;
    pthread_mutex_t mutex;
} msg_bus_t;

int msg_bus_init(msg_bus_t* bus);

void msg_bus_destroy(msg_bus_t* bus);

int msg_bus_subscribe(msg_bus_t* bus, const char* msg_type, msg_handler_t handler, void* user_data);

int msg_bus_unsubscribe(msg_bus_t* bus, const char* msg_type);

int msg_bus_publish(msg_bus_t* bus, const char* msg_type, const char* msg_id, const char* payload);

#ifdef __cplusplus
}
#endif

#endif