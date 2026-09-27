#include "test_common.h"
#include "../src/common/include/log.h"
#include "../src/common/include/config.h"
#include "../src/common/include/common.h"
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

void test_log_init(void** state) {
    log_config_t config;
    memset(&config, 0, sizeof(config));
    config.level = LOG_LEVEL_DEBUG;
    
    int rc = log_init(&config);
    assert_int_equal(rc, 0);
}

void test_log_write(void** state) {
    LOG_DEBUG("TEST", "Debug message");
    LOG_INFO("TEST", "Info message");
    LOG_WARN("TEST", "Warn message");
    LOG_ERROR("TEST", "Error message");
    LOG_FATAL("TEST", "Fatal message");
}

void test_log_deinit(void** state) {
    log_deinit();
}

void test_config_create_destroy(void** state) {
    config_t* config = config_create(NULL);
    assert_non_null(config);
    
    config_destroy(config);
}

void test_config_set_get(void** state) {
    config_t* config = config_create(NULL);
    assert_non_null(config);
    
    int rc = config_set(config, "test_key", "test_value");
    assert_int_equal(rc, 0);
    
    const char* value = config_get(config, "test_key", NULL);
    assert_non_null(value);
    assert_string_equal(value, "test_value");
    
    const char* default_value = config_get(config, "non_exist", "default");
    assert_string_equal(default_value, "default");
    
    rc = config_set_int(config, "int_key", 42);
    assert_int_equal(rc, 0);
    
    int int_val = config_get_int(config, "int_key", 0);
    assert_int_equal(int_val, 42);
    
    config_destroy(config);
}

void test_config_load_save(void** state) {
    const char* test_file = "/tmp/test_config.conf";
    
    FILE* f = fopen(test_file, "w");
    fprintf(f, "# Test config\nkey1=value1\nkey2=value2\n");
    fclose(f);
    
    config_t* config = config_create(test_file);
    assert_non_null(config);
    
    int rc = config_load(config);
    assert_int_equal(rc, 0);
    
    const char* v1 = config_get(config, "key1", NULL);
    assert_string_equal(v1, "value1");
    
    const char* v2 = config_get(config, "key2", NULL);
    assert_string_equal(v2, "value2");
    
    config_set(config, "key3", "value3");
    rc = config_save(config);
    assert_int_equal(rc, 0);
    
    config_destroy(config);
    
    config = config_create(test_file);
    rc = config_load(config);
    assert_int_equal(rc, 0);
    
    const char* v3 = config_get(config, "key3", NULL);
    assert_string_equal(v3, "value3");
    
    config_destroy(config);
    remove(test_file);
}

void test_generate_msgid(void** state) {
    char* msgid = generate_msgid("SN001");
    assert_non_null(msgid);
    
    assert_true(strlen(msgid) > 0);
    assert_true(strstr(msgid, "SN001") != NULL);
    
    free(msgid);
}

void test_free_msg(void** state) {
    msg_t* msg = (msg_t*)malloc(sizeof(msg_t));
    memset(msg, 0, sizeof(msg_t));
    msg->payload = strdup("test payload");
    
    free_msg(msg);
}