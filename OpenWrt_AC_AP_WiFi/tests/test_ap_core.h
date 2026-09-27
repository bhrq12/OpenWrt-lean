#ifndef TEST_AP_CORE_H
#define TEST_AP_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

void test_config_adapter_init_destroy(void** state);
void test_config_adapter_validate(void** state);
void test_config_adapter_apply(void** state);
void test_config_adapter_rollback(void** state);

#ifdef __cplusplus
}
#endif

#endif