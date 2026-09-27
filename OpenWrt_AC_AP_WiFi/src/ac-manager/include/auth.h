#ifndef AUTH_H
#define AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>

#define MAX_ROLE_NAME 64
#define MAX_PERMISSION_NAME 128
#define MAX_USER_NAME 64
#define MAX_SITE_ID 32
#define MAX_AUDIT_LOG_LEN 1024
#define MAX_AUDIT_LOGS 1024

typedef enum {
    ROLE_ADMIN = 0,
    ROLE_SITE_ADMIN = 1,
    ROLE_GROUP_ADMIN = 2,
    ROLE_OBSERVER = 3,
    ROLE_API_USER = 4
} role_type_t;

typedef struct {
    char name[MAX_PERMISSION_NAME];
    uint32_t mask;
} permission_t;

typedef struct {
    role_type_t role;
    char role_name[MAX_ROLE_NAME];
    uint32_t permissions;
} role_t;

typedef struct {
    char user_id[MAX_USER_NAME];
    char username[MAX_USER_NAME];
    role_t role;
    char site_id[MAX_SITE_ID];
    char cert_serial[MAX_SN_LEN];
    time_t login_time;
    time_t last_access_time;
    int active;
} user_session_t;

typedef struct {
    char log_id[MAX_SN_LEN];
    char user_id[MAX_USER_NAME];
    char site_id[MAX_SITE_ID];
    char action[MAX_PERMISSION_NAME];
    char resource[MAX_SN_LEN];
    char result[MAX_PERMISSION_NAME];
    char detail[MAX_AUDIT_LOG_LEN];
    time_t timestamp;
} audit_log_t;

typedef struct {
    role_t roles[16];
    int role_count;
    user_session_t sessions[128];
    int session_count;
    audit_log_t audit_logs[MAX_AUDIT_LOGS];
    int audit_log_count;
    pthread_rwlock_t lock;
    pthread_mutex_t audit_mutex;
} auth_manager_t;

typedef enum {
    AUTH_RESULT_SUCCESS = 0,
    AUTH_RESULT_NO_CERT = -1,
    AUTH_RESULT_INVALID_CERT = -2,
    AUTH_RESULT_EXPIRED_CERT = -3,
    AUTH_RESULT_UNKNOWN_CERT = -4,
    AUTH_RESULT_NO_PERMISSION = -5,
    AUTH_RESULT_SESSION_EXPIRED = -6,
    AUTH_RESULT_USER_NOT_FOUND = -7
} auth_result_t;

int auth_manager_init(auth_manager_t* manager);

void auth_manager_destroy(auth_manager_t* manager);

auth_result_t auth_validate_client_cert(auth_manager_t* manager, const char* cert_data, 
                                        const char* cert_serial, user_session_t* session);

auth_result_t auth_check_permission(auth_manager_t* manager, const char* user_id, 
                                    const char* action, const char* resource, const char* site_id);

auth_result_t auth_create_session(auth_manager_t* manager, const char* user_id, 
                                  const char* cert_serial, const char* site_id,
                                  user_session_t** session);

void auth_destroy_session(auth_manager_t* manager, const char* user_id);

int auth_log_action(auth_manager_t* manager, const char* user_id, const char* site_id,
                    const char* action, const char* resource, const char* result,
                    const char* detail);

int auth_get_audit_logs(auth_manager_t* manager, audit_log_t** logs, int* count);

void auth_free_audit_logs(audit_log_t* logs, int count);

const char* auth_result_to_string(auth_result_t result);

#ifdef __cplusplus
}
#endif

#endif