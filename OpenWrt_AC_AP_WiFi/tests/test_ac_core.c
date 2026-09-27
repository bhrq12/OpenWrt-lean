#include "test_ac_core.h"
#include "../src/ac-manager/include/msg_bus.h"
#include "../src/ac-manager/include/device_manager.h"
#include "../src/ac-manager/include/config_manager.h"
#include "../src/ac-manager/include/auth.h"
#include "../src/common/include/log.h"
#include <cmocka.h>
#include <string.h>
#include <time.h>

static int g_handler_called = 0;
static const char* g_last_msg_type = NULL;
static const char* g_last_payload = NULL;

static int test_handler(const char* msg_type, const char* msg_id, const char* payload, void* user_data) {
    g_handler_called++;
    g_last_msg_type = msg_type;
    g_last_payload = payload;
    return 0;
}

void test_msg_bus_init_destroy(void** state) {
    msg_bus_t bus;
    int rc = msg_bus_init(&bus);
    assert_int_equal(rc, 0);
    
    msg_bus_destroy(&bus);
}

void test_msg_bus_subscribe_publish(void** state) {
    msg_bus_t bus;
    msg_bus_init(&bus);
    
    g_handler_called = 0;
    
    int rc = msg_bus_subscribe(&bus, "test_type", test_handler, NULL);
    assert_int_equal(rc, 0);
    
    rc = msg_bus_publish(&bus, "test_type", "msg001", "{\"data\":123}");
    assert_int_equal(rc, 0);
    
    assert_int_equal(g_handler_called, 1);
    assert_string_equal(g_last_msg_type, "test_type");
    
    msg_bus_destroy(&bus);
}

void test_msg_bus_unsubscribe(void** state) {
    msg_bus_t bus;
    msg_bus_init(&bus);
    
    msg_bus_subscribe(&bus, "test_type", test_handler, NULL);
    int rc = msg_bus_unsubscribe(&bus, "test_type");
    assert_int_equal(rc, 0);
    
    g_handler_called = 0;
    msg_bus_publish(&bus, "test_type", "msg001", "data");
    assert_int_equal(g_handler_called, 0);
    
    msg_bus_destroy(&bus);
}

void test_device_manager_init_destroy(void** state) {
    device_manager_t dm;
    int rc = device_manager_init(&dm);
    assert_int_equal(rc, 0);
    
    device_manager_destroy(&dm);
}

void test_device_manager_add_get(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN001", sizeof(info.dev_sn) - 1);
    strncpy(info.dev_model, "ModelA", sizeof(info.dev_model) - 1);
    strncpy(info.fw_version, "1.0.0", sizeof(info.fw_version) - 1);
    info.status = DEV_ONLINE;
    
    int rc = device_manager_add(&dm, &info);
    assert_int_equal(rc, 0);
    
    device_info_t* result = device_manager_get(&dm, "SN001");
    assert_non_null(result);
    assert_string_equal(result->dev_sn, "SN001");
    assert_string_equal(result->dev_model, "ModelA");
    assert_int_equal(result->status, DEV_ONLINE);
    
    free(result);
    device_manager_destroy(&dm);
}

void test_device_manager_update_status(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN002", sizeof(info.dev_sn) - 1);
    info.status = DEV_ONLINE;
    
    device_manager_add(&dm, &info);
    
    int rc = device_manager_update_status(&dm, "SN002", DEV_UPGRADING);
    assert_int_equal(rc, 0);
    
    device_info_t* result = device_manager_get(&dm, "SN002");
    assert_non_null(result);
    assert_int_equal(result->status, DEV_UPGRADING);
    
    free(result);
    device_manager_destroy(&dm);
}

void test_device_manager_remove(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN003", sizeof(info.dev_sn) - 1);
    
    device_manager_add(&dm, &info);
    
    int rc = device_manager_remove(&dm, "SN003");
    assert_int_equal(rc, 0);
    
    device_info_t* result = device_manager_get(&dm, "SN003");
    assert_null(result);
    
    device_manager_destroy(&dm);
}

void test_device_manager_get_count(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    int count = device_manager_get_count(&dm);
    assert_int_equal(count, 0);
    
    device_info_t info1, info2;
    memset(&info1, 0, sizeof(info1));
    memset(&info2, 0, sizeof(info2));
    strncpy(info1.dev_sn, "SN004", sizeof(info1.dev_sn) - 1);
    strncpy(info2.dev_sn, "SN005", sizeof(info2.dev_sn) - 1);
    info1.status = DEV_ONLINE;
    info2.status = DEV_OFFLINE;
    
    device_manager_add(&dm, &info1);
    device_manager_add(&dm, &info2);
    
    count = device_manager_get_count(&dm);
    assert_int_equal(count, 2);
    
    int online_count = device_manager_get_online_count(&dm);
    assert_int_equal(online_count, 1);
    
    device_manager_destroy(&dm);
}

static int g_timeout_callback_count = 0;

static void test_timeout_callback(const char* dev_sn, void* user_data) {
    g_timeout_callback_count++;
}

void test_device_manager_check_timeout(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN006", sizeof(info.dev_sn) - 1);
    info.status = DEV_ONLINE;
    info.last_heartbeat = time(NULL) - 100;
    
    device_manager_add(&dm, &info);
    
    g_timeout_callback_count = 0;
    int timeout_count = device_manager_check_timeout(&dm, 50, test_timeout_callback, NULL);
    assert_int_equal(timeout_count, 1);
    assert_int_equal(g_timeout_callback_count, 1);
    
    device_manager_destroy(&dm);
}

void test_config_manager_init_destroy(void** state) {
    config_manager_t cm;
    int rc = config_manager_init(&cm);
    assert_int_equal(rc, 0);
    
    config_manager_destroy(&cm);
}

void test_config_manager_add_get(void** state) {
    config_manager_t cm;
    config_manager_init(&cm);
    
    config_template_t template;
    memset(&template, 0, sizeof(template));
    strncpy(template.config_id, "CFG001", sizeof(template.config_id) - 1);
    strncpy(template.template_name, "TestTemplate", sizeof(template.template_name) - 1);
    template.template_type = CONFIG_TYPE_SSID;
    template.version = 1;
    template.config_content = strdup("{\"ssid\":\"TestSSID\"}");
    
    int rc = config_manager_add_template(&cm, &template);
    assert_int_equal(rc, 0);
    
    config_template_t* result = config_manager_get_template(&cm, "CFG001");
    assert_non_null(result);
    assert_string_equal(result->config_id, "CFG001");
    assert_int_equal(result->version, 1);
    
    free(result->config_content);
    free(result);
    free(template.config_content);
    config_manager_destroy(&cm);
}

void test_config_manager_update(void** state) {
    config_manager_t cm;
    config_manager_init(&cm);
    
    config_template_t template;
    memset(&template, 0, sizeof(template));
    strncpy(template.config_id, "CFG002", sizeof(template.config_id) - 1);
    template.version = 1;
    template.config_content = strdup("{\"ssid\":\"V1\"}");
    
    config_manager_add_template(&cm, &template);
    
    template.version = 2;
    template.config_content = strdup("{\"ssid\":\"V2\"}");
    
    int rc = config_manager_update_template(&cm, &template);
    assert_int_equal(rc, 0);
    
    config_template_t* result = config_manager_get_template(&cm, "CFG002");
    assert_non_null(result);
    assert_int_equal(result->version, 2);
    
    free(result->config_content);
    free(result);
    free(template.config_content);
    config_manager_destroy(&cm);
}

void test_config_manager_delete(void** state) {
    config_manager_t cm;
    config_manager_init(&cm);
    
    config_template_t template;
    memset(&template, 0, sizeof(template));
    strncpy(template.config_id, "CFG003", sizeof(template.config_id) - 1);
    template.config_content = strdup("{}");
    
    config_manager_add_template(&cm, &template);
    
    int rc = config_manager_delete_template(&cm, "CFG003");
    assert_int_equal(rc, 0);
    
    config_template_t* result = config_manager_get_template(&cm, "CFG003");
    assert_null(result);
    
    free(template.config_content);
    config_manager_destroy(&cm);
}

void test_config_manager_signature(void** state) {
    const char* content = "{\"test\":\"data\"}";
    char signature[256];
    
    int rc = config_manager_generate_signature(content, signature, sizeof(signature));
    assert_int_equal(rc, 0);
    assert_true(strlen(signature) == 64);
    
    rc = config_manager_verify_signature(content, signature);
    assert_int_equal(rc, 0);
    
    char wrong_signature[256] = "0000000000000000000000000000000000000000000000000000000000000000";
    rc = config_manager_verify_signature(content, wrong_signature);
    assert_int_not_equal(rc, 0);
}

void test_msg_bus_null_params(void** state) {
    msg_bus_t bus;
    msg_bus_init(&bus);
    
    int rc = msg_bus_publish(NULL, "test", "msg001", "data");
    assert_int_equal(rc, -1);
    
    rc = msg_bus_subscribe(NULL, "test", test_handler, NULL);
    assert_int_equal(rc, -1);
    
    rc = msg_bus_publish(&bus, NULL, "msg001", "data");
    assert_int_equal(rc, -1);
    
    msg_bus_destroy(&bus);
}

void test_msg_bus_empty_payload(void** state) {
    msg_bus_t bus;
    msg_bus_init(&bus);
    
    g_handler_called = 0;
    msg_bus_subscribe(&bus, "test_type", test_handler, NULL);
    
    int rc = msg_bus_publish(&bus, "test_type", "msg001", "");
    assert_int_equal(rc, 0);
    assert_int_equal(g_handler_called, 1);
    
    msg_bus_destroy(&bus);
}

void test_device_manager_null_params(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    int rc = device_manager_add(NULL, NULL);
    assert_int_equal(rc, -1);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN001", sizeof(info.dev_sn) - 1);
    
    rc = device_manager_add(NULL, &info);
    assert_int_equal(rc, -1);
    
    rc = device_manager_add(&dm, NULL);
    assert_int_equal(rc, -1);
    
    rc = device_manager_get(NULL, "SN001");
    assert_null(rc);
    
    rc = device_manager_remove(NULL, "SN001");
    assert_int_equal(rc, -1);
    
    device_manager_destroy(&dm);
}

void test_device_manager_duplicate_add(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    device_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.dev_sn, "SN001", sizeof(info.dev_sn) - 1);
    info.status = DEV_ONLINE;
    
    int rc = device_manager_add(&dm, &info);
    assert_int_equal(rc, 0);
    
    info.status = DEV_UPGRADING;
    rc = device_manager_add(&dm, &info);
    assert_int_equal(rc, 0);
    
    device_info_t* result = device_manager_get(&dm, "SN001");
    assert_non_null(result);
    assert_int_equal(result->status, DEV_UPGRADING);
    
    free(result);
    device_manager_destroy(&dm);
}

void test_device_manager_remove_nonexistent(void** state) {
    device_manager_t dm;
    device_manager_init(&dm);
    
    int rc = device_manager_remove(&dm, "NONEXISTENT");
    assert_int_equal(rc, -1);
    
    int count = device_manager_get_count(&dm);
    assert_int_equal(count, 0);
    
    device_manager_destroy(&dm);
}

void test_config_manager_null_params(void** state) {
    config_manager_t cm;
    config_manager_init(&cm);
    
    config_template_t template;
    memset(&template, 0, sizeof(template));
    strncpy(template.config_id, "CFG001", sizeof(template.config_id) - 1);
    
    int rc = config_manager_add_template(NULL, &template);
    assert_int_equal(rc, -1);
    
    rc = config_manager_add_template(&cm, NULL);
    assert_int_equal(rc, -1);
    
    rc = config_manager_get_template(NULL, "CFG001");
    assert_null(rc);
    
    rc = config_manager_delete_template(NULL, "CFG001");
    assert_int_equal(rc, -1);
    
    config_manager_destroy(&cm);
}

void test_config_manager_empty_content(void** state) {
    config_manager_t cm;
    config_manager_init(&cm);
    
    config_template_t template;
    memset(&template, 0, sizeof(template));
    strncpy(template.config_id, "CFG001", sizeof(template.config_id) - 1);
    template.config_content = strdup("");
    
    int rc = config_manager_add_template(&cm, &template);
    assert_int_equal(rc, 0);
    
    config_template_t* result = config_manager_get_template(&cm, "CFG001");
    assert_non_null(result);
    assert_string_equal(result->config_id, "CFG001");
    
    free(result->config_content);
    free(result);
    free(template.config_content);
    config_manager_destroy(&cm);
}

void test_auth_manager_init_destroy(void** state) {
    auth_manager_t am;
    int rc = auth_manager_init(&am);
    assert_int_equal(rc, 0);
    
    auth_manager_destroy(&am);
}

void test_auth_check_permission(void** state) {
    auth_manager_t am;
    auth_manager_init(&am);
    
    user_session_t* session = NULL;
    int rc = auth_create_session(&am, "test_user", NULL, NULL, &session);
    assert_int_equal(rc, AUTH_RESULT_SUCCESS);
    assert_non_null(session);
    
    auth_result_t auth_result = auth_check_permission(&am, "test_user", "config_view", NULL, NULL);
    assert_int_equal(auth_result, AUTH_RESULT_SUCCESS);
    
    auth_result = auth_check_permission(&am, "test_user", "device_delete", NULL, NULL);
    assert_int_equal(auth_result, AUTH_RESULT_NO_PERMISSION);
    
    auth_destroy_session(&am, "test_user");
    
    auth_manager_destroy(&am);
}

void test_auth_session_expire(void** state) {
    auth_manager_t am;
    auth_manager_init(&am);
    
    user_session_t* session = NULL;
    auth_create_session(&am, "test_user", NULL, NULL, &session);
    assert_non_null(session);
    
    session->last_access_time = time(NULL) - 4000;
    
    auth_result_t auth_result = auth_check_permission(&am, "test_user", "config_view", NULL, NULL);
    assert_int_equal(auth_result, AUTH_RESULT_SESSION_EXPIRED);
    
    auth_manager_destroy(&am);
}