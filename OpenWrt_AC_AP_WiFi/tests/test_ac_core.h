#ifndef TEST_AC_CORE_H
#define TEST_AC_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

void test_msg_bus_init_destroy(void** state);
void test_msg_bus_subscribe_publish(void** state);
void test_msg_bus_unsubscribe(void** state);
void test_msg_bus_null_params(void** state);
void test_msg_bus_empty_payload(void** state);

void test_device_manager_init_destroy(void** state);
void test_device_manager_add_get(void** state);
void test_device_manager_update_status(void** state);
void test_device_manager_remove(void** state);
void test_device_manager_get_count(void** state);
void test_device_manager_check_timeout(void** state);
void test_device_manager_null_params(void** state);
void test_device_manager_duplicate_add(void** state);
void test_device_manager_remove_nonexistent(void** state);

void test_config_manager_init_destroy(void** state);
void test_config_manager_add_get(void** state);
void test_config_manager_update(void** state);
void test_config_manager_delete(void** state);
void test_config_manager_signature(void** state);
void test_config_manager_null_params(void** state);
void test_config_manager_empty_content(void** state);

void test_auth_manager_init_destroy(void** state);
void test_auth_check_permission(void** state);
void test_auth_session_expire(void** state);

#ifdef __cplusplus
}
#endif

#endif