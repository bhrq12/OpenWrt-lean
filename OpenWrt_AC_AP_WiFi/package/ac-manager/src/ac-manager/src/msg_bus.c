#include "msg_bus.h"
#include "common/include/log.h"
#include <pthread.h>
#include <string.h>

int msg_bus_init(msg_bus_t* bus) {
    if (!bus) {
        return -1;
    }
    
    memset(bus, 0, sizeof(msg_bus_t));
    if (pthread_mutex_init(&bus->mutex, NULL) != 0) {
        LOG_ERROR("MSG_BUS", "Failed to init mutex");
        return -1;
    }
    
    LOG_INFO("MSG_BUS", "Message bus initialized");
    return 0;
}

void msg_bus_destroy(msg_bus_t* bus) {
    if (!bus) {
        return;
    }
    
    msg_handler_node_t* node = bus->handlers;
    while (node) {
        msg_handler_node_t* next = node->next;
        free(node);
        node = next;
    }
    
    pthread_mutex_destroy(&bus->mutex);
}

int msg_bus_subscribe(msg_bus_t* bus, const char* msg_type, msg_handler_t handler, void* user_data) {
    if (!bus || !msg_type || !handler) {
        return -1;
    }
    
    pthread_mutex_lock(&bus->mutex);
    
    msg_handler_node_t* node = bus->handlers;
    while (node) {
        if (strcmp(node->msg_type, msg_type) == 0) {
            pthread_mutex_unlock(&bus->mutex);
            LOG_WARN("MSG_BUS", "Handler already subscribed for type: %s", msg_type);
            return -1;
        }
        node = node->next;
    }
    
    msg_handler_node_t* new_node = (msg_handler_node_t*)malloc(sizeof(msg_handler_node_t));
    if (!new_node) {
        pthread_mutex_unlock(&bus->mutex);
        LOG_ERROR("MSG_BUS", "Failed to allocate handler node");
        return -1;
    }
    
    strncpy(new_node->msg_type, msg_type, sizeof(new_node->msg_type) - 1);
    new_node->handler = handler;
    new_node->user_data = user_data;
    new_node->next = bus->handlers;
    bus->handlers = new_node;
    
    pthread_mutex_unlock(&bus->mutex);
    
    LOG_DEBUG("MSG_BUS", "Subscribed handler for type: %s", msg_type);
    return 0;
}

int msg_bus_unsubscribe(msg_bus_t* bus, const char* msg_type) {
    if (!bus || !msg_type) {
        return -1;
    }
    
    pthread_mutex_lock(&bus->mutex);
    
    msg_handler_node_t** p = &bus->handlers;
    while (*p) {
        if (strcmp((*p)->msg_type, msg_type) == 0) {
            msg_handler_node_t* node = *p;
            *p = node->next;
            free(node);
            pthread_mutex_unlock(&bus->mutex);
            LOG_DEBUG("MSG_BUS", "Unsubscribed handler for type: %s", msg_type);
            return 0;
        }
        p = &(*p)->next;
    }
    
    pthread_mutex_unlock(&bus->mutex);
    LOG_WARN("MSG_BUS", "No handler found for type: %s", msg_type);
    return -1;
}

int msg_bus_publish(msg_bus_t* bus, const char* msg_type, const char* msg_id, const char* payload) {
    if (!bus || !msg_type) {
        return -1;
    }
    
    pthread_mutex_lock(&bus->mutex);
    
    msg_handler_node_t* node = bus->handlers;
    int found = 0;
    
    while (node) {
        if (strcmp(node->msg_type, msg_type) == 0) {
            found = 1;
            int ret = node->handler(msg_type, msg_id, payload, node->user_data);
            if (ret != 0) {
                LOG_WARN("MSG_BUS", "Handler for %s returned error: %d", msg_type, ret);
            }
        }
        node = node->next;
    }
    
    pthread_mutex_unlock(&bus->mutex);
    
    if (!found) {
        LOG_DEBUG("MSG_BUS", "No handler found for type: %s", msg_type);
        return -1;
    }
    
    return 0;
}