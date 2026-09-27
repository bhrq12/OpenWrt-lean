#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

void test_log_init(void** state);
void test_log_write(void** state);
void test_log_deinit(void** state);

void test_config_create_destroy(void** state);
void test_config_set_get(void** state);
void test_config_load_save(void** state);

void test_generate_msgid(void** state);
void test_free_msg(void** state);

#ifdef __cplusplus
}
#endif

#endif