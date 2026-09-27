#include "common.h"

char* generate_msgid(const char* dev_sn) {
    static char msgid[MAX_MSG_ID_LEN];
    time_t now = time(NULL);
    snprintf(msgid, sizeof(msgid), "%lu-%s", (unsigned long)now, dev_sn ? dev_sn : "unknown");
    return strdup(msgid);
}

void free_msg(msg_t* msg) {
    if (msg) {
        if (msg->payload) {
            free(msg->payload);
            msg->payload = NULL;
        }
        free(msg);
    }
}