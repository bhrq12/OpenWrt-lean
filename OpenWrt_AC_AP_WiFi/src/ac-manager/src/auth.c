#include "auth.h"
#include "../common/include/log.h"
#include "../common/include/common.h"
#include <string.h>
#include <stdlib.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

static const permission_t g_permissions[] = {
    {"device_view", 0x0001},
    {"device_edit", 0x0002},
    {"device_delete", 0x0004},
    {"config_view", 0x0008},
    {"config_edit", 0x0010},
    {"config_delete", 0x0020},
    {"site_view", 0x0040},
    {"site_edit", 0x0080},
    {"user_view", 0x0100},
    {"user_edit", 0x0200},
    {"audit_view", 0x0400},
    {"system_admin", 0x8000}
};

static const int g_permission_count = sizeof(g_permissions) / sizeof(g_permissions[0]);

static const role_t g_role_definitions[] = {
    {ROLE_ADMIN, "admin", 0xFFFF},
    {ROLE_SITE_ADMIN, "site_admin", 0x00FF},
    {ROLE_GROUP_ADMIN, "group_admin", 0x003F},
    {ROLE_OBSERVER, "observer", 0x0009},
    {ROLE_API_USER, "api_user", 0x0018}
};

static const int g_role_def_count = sizeof(g_role_definitions) / sizeof(g_role_definitions[0]);

int auth_manager_init(auth_manager_t* manager) {
    if (!manager) {
        return -1;
    }
    
    memset(manager, 0, sizeof(auth_manager_t));
    
    for (int i = 0; i < g_role_def_count && i < 16; i++) {
        memcpy(&manager->roles[i], &g_role_definitions[i], sizeof(role_t));
        manager->role_count++;
    }
    
    pthread_rwlock_init(&manager->lock, NULL);
    pthread_mutex_init(&manager->audit_mutex, NULL);
    
    LOG_INFO("AUTH", "Auth manager initialized");
    return 0;
}

void auth_manager_destroy(auth_manager_t* manager) {
    if (!manager) {
        return;
    }
    
    pthread_rwlock_destroy(&manager->lock);
    pthread_mutex_destroy(&manager->audit_mutex);
    
    LOG_INFO("AUTH", "Auth manager destroyed");
}

static uint32_t auth_get_permission_mask(const char* permission_name) {
    for (int i = 0; i < g_permission_count; i++) {
        if (strcmp(g_permissions[i].name, permission_name) == 0) {
            return g_permissions[i].mask;
        }
    }
    return 0;
}

static role_t* auth_get_role(auth_manager_t* manager, role_type_t role) {
    pthread_rwlock_rdlock(&manager->lock);
    for (int i = 0; i < manager->role_count; i++) {
        if (manager->roles[i].role == role) {
            pthread_rwlock_unlock(&manager->lock);
            return &manager->roles[i];
        }
    }
    pthread_rwlock_unlock(&manager->lock);
    return NULL;
}

auth_result_t auth_validate_client_cert(auth_manager_t* manager, const char* cert_data,
                                        const char* cert_serial, user_session_t* session) {
    if (!manager || !cert_data || !session) {
        return AUTH_RESULT_NO_CERT;
    }
    
    BIO* bio = BIO_new_mem_buf(cert_data, -1);
    if (!bio) {
        LOG_ERROR("AUTH", "Failed to create BIO from cert data");
        return AUTH_RESULT_INVALID_CERT;
    }
    
    X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    BIO_free(bio);
    
    if (!cert) {
        LOG_ERROR("AUTH", "Failed to parse certificate");
        return AUTH_RESULT_INVALID_CERT;
    }
    
    ASN1_TIME* not_after = X509_get_notAfter(cert);
    if (X509_cmp_time(not_after, ASN1_TIME_new()) < 0) {
        X509_free(cert);
        LOG_ERROR("AUTH", "Certificate expired");
        return AUTH_RESULT_EXPIRED_CERT;
    }
    
    X509_NAME* subject = X509_get_subject_name(cert);
    char subject_name[256];
    X509_NAME_oneline(subject, subject_name, sizeof(subject_name));
    
    X509_free(cert);
    
    pthread_rwlock_wrlock(&manager->lock);
    
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].cert_serial, cert_serial) == 0) {
            memcpy(session, &manager->sessions[i], sizeof(user_session_t));
            session->last_access_time = time(NULL);
            manager->sessions[i].last_access_time = session->last_access_time;
            pthread_rwlock_unlock(&manager->lock);
            LOG_INFO("AUTH", "Cert validated for user: %s", session->username);
            return AUTH_RESULT_SUCCESS;
        }
    }
    
    if (manager->session_count >= 128) {
        pthread_rwlock_unlock(&manager->lock);
        LOG_ERROR("AUTH", "Session limit reached");
        return AUTH_RESULT_NO_PERMISSION;
    }
    
    user_session_t* new_session = &manager->sessions[manager->session_count++];
    memset(new_session, 0, sizeof(user_session_t));
    
    snprintf(new_session->user_id, sizeof(new_session->user_id), "user_%s", cert_serial);
    snprintf(new_session->username, sizeof(new_session->username), "cert_%s", cert_serial);
    strncpy(new_session->cert_serial, cert_serial, sizeof(new_session->cert_serial) - 1);
    new_session->role.role = ROLE_API_USER;
    strncpy(new_session->role.role_name, "api_user", sizeof(new_session->role.role_name) - 1);
    new_session->role.permissions = g_role_definitions[ROLE_API_USER].permissions;
    new_session->login_time = time(NULL);
    new_session->last_access_time = new_session->login_time;
    new_session->active = 1;
    
    pthread_rwlock_unlock(&manager->lock);
    
    memcpy(session, new_session, sizeof(user_session_t));
    
    LOG_INFO("AUTH", "New cert-based session created: %s", session->user_id);
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_check_permission(auth_manager_t* manager, const char* user_id,
                                    const char* action, const char* resource, const char* site_id) {
    if (!manager || !user_id || !action) {
        return AUTH_RESULT_USER_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    user_session_t* session = NULL;
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].user_id, user_id) == 0) {
            session = &manager->sessions[i];
            break;
        }
    }
    
    if (!session || !session->active) {
        pthread_rwlock_unlock(&manager->lock);
        return AUTH_RESULT_SESSION_EXPIRED;
    }
    
    time_t now = time(NULL);
    if (now - session->last_access_time > 3600) {
        pthread_rwlock_unlock(&manager->lock);
        return AUTH_RESULT_SESSION_EXPIRED;
    }
    
    session->last_access_time = now;
    
    uint32_t required_perm = auth_get_permission_mask(action);
    if (!required_perm) {
        pthread_rwlock_unlock(&manager->lock);
        LOG_WARN("AUTH", "Unknown permission: %s", action);
        return AUTH_RESULT_NO_PERMISSION;
    }
    
    if (!(session->role.permissions & required_perm)) {
        pthread_rwlock_unlock(&manager->lock);
        LOG_WARN("AUTH", "Permission denied for user %s: %s", user_id, action);
        return AUTH_RESULT_NO_PERMISSION;
    }
    
    if (site_id && session->role.role != ROLE_ADMIN && 
        strcmp(session->site_id, site_id) != 0 && strlen(session->site_id) > 0) {
        pthread_rwlock_unlock(&manager->lock);
        LOG_WARN("AUTH", "Site access denied for user %s", user_id);
        return AUTH_RESULT_NO_PERMISSION;
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    LOG_DEBUG("AUTH", "Permission granted for user %s: %s", user_id, action);
    return AUTH_RESULT_SUCCESS;
}

auth_result_t auth_create_session(auth_manager_t* manager, const char* user_id,
                                  const char* cert_serial, const char* site_id,
                                  user_session_t** session) {
    if (!manager || !user_id) {
        return AUTH_RESULT_USER_NOT_FOUND;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].user_id, user_id) == 0) {
            pthread_rwlock_unlock(&manager->lock);
            *session = &manager->sessions[i];
            return AUTH_RESULT_SUCCESS;
        }
    }
    
    if (manager->session_count >= 128) {
        pthread_rwlock_unlock(&manager->lock);
        LOG_ERROR("AUTH", "Session limit reached");
        return AUTH_RESULT_NO_PERMISSION;
    }
    
    user_session_t* new_session = &manager->sessions[manager->session_count++];
    memset(new_session, 0, sizeof(user_session_t));
    
    strncpy(new_session->user_id, user_id, sizeof(new_session->user_id) - 1);
    strncpy(new_session->username, user_id, sizeof(new_session->username) - 1);
    
    if (cert_serial) {
        strncpy(new_session->cert_serial, cert_serial, sizeof(new_session->cert_serial) - 1);
    }
    
    if (site_id) {
        strncpy(new_session->site_id, site_id, sizeof(new_session->site_id) - 1);
    }
    
    new_session->role.role = ROLE_API_USER;
    strncpy(new_session->role.role_name, "api_user", sizeof(new_session->role.role_name) - 1);
    new_session->role.permissions = g_role_definitions[ROLE_API_USER].permissions;
    
    new_session->login_time = time(NULL);
    new_session->last_access_time = new_session->login_time;
    new_session->active = 1;
    
    pthread_rwlock_unlock(&manager->lock);
    
    *session = new_session;
    LOG_INFO("AUTH", "Session created for user: %s", user_id);
    
    return AUTH_RESULT_SUCCESS;
}

void auth_destroy_session(auth_manager_t* manager, const char* user_id) {
    if (!manager || !user_id) {
        return;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].user_id, user_id) == 0) {
            manager->sessions[i].active = 0;
            
            for (int j = i; j < manager->session_count - 1; j++) {
                memcpy(&manager->sessions[j], &manager->sessions[j + 1], sizeof(user_session_t));
            }
            manager->session_count--;
            
            LOG_INFO("AUTH", "Session destroyed for user: %s", user_id);
            break;
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
}

int auth_log_action(auth_manager_t* manager, const char* user_id, const char* site_id,
                    const char* action, const char* resource, const char* result,
                    const char* detail) {
    if (!manager || !user_id || !action) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->audit_mutex);
    
    int idx = manager->audit_log_count % MAX_AUDIT_LOGS;
    audit_log_t* log = &manager->audit_logs[idx];
    
    memset(log, 0, sizeof(audit_log_t));
    
    generate_uuid(log->log_id, sizeof(log->log_id));
    strncpy(log->user_id, user_id, sizeof(log->user_id) - 1);
    
    if (site_id) {
        strncpy(log->site_id, site_id, sizeof(log->site_id) - 1);
    }
    
    strncpy(log->action, action, sizeof(log->action) - 1);
    
    if (resource) {
        strncpy(log->resource, resource, sizeof(log->resource) - 1);
    }
    
    strncpy(log->result, result ? result : "unknown", sizeof(log->result) - 1);
    
    if (detail) {
        strncpy(log->detail, detail, sizeof(log->detail) - 1);
    }
    
    log->timestamp = time(NULL);
    
    if (manager->audit_log_count < MAX_AUDIT_LOGS) {
        manager->audit_log_count++;
    }
    
    pthread_mutex_unlock(&manager->audit_mutex);
    
    LOG_INFO("AUDIT", "[%s] User=%s Action=%s Resource=%s Result=%s",
             log->log_id, user_id, action, resource ? resource : "-", result ? result : "-");
    
    return 0;
}

int auth_get_audit_logs(auth_manager_t* manager, audit_log_t** logs, int* count) {
    if (!manager || !logs || !count) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->audit_mutex);
    
    *count = manager->audit_log_count;
    *logs = (audit_log_t*)malloc(*count * sizeof(audit_log_t));
    
    if (!*logs) {
        pthread_mutex_unlock(&manager->audit_mutex);
        return -1;
    }
    
    for (int i = 0; i < *count; i++) {
        memcpy(&(*logs)[i], &manager->audit_logs[i], sizeof(audit_log_t));
    }
    
    pthread_mutex_unlock(&manager->audit_mutex);
    
    return 0;
}

void auth_free_audit_logs(audit_log_t* logs, int count) {
    if (logs) {
        free(logs);
    }
}

const char* auth_result_to_string(auth_result_t result) {
    switch (result) {
        case AUTH_RESULT_SUCCESS: return "success";
        case AUTH_RESULT_NO_CERT: return "no_cert";
        case AUTH_RESULT_INVALID_CERT: return "invalid_cert";
        case AUTH_RESULT_EXPIRED_CERT: return "expired_cert";
        case AUTH_RESULT_UNKNOWN_CERT: return "unknown_cert";
        case AUTH_RESULT_NO_PERMISSION: return "no_permission";
        case AUTH_RESULT_SESSION_EXPIRED: return "session_expired";
        case AUTH_RESULT_USER_NOT_FOUND: return "user_not_found";
        default: return "unknown";
    }
}