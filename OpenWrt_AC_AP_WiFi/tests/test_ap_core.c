#include "test_ap_core.h"
#include "../src/ap-manager/include/config_adapter.h"
#include "../src/common/include/log.h"
#include <cmocka.h>
#include <string.h>

void test_config_adapter_init_destroy(void** state) {
    config_adapter_t adapter;
    int rc = config_adapter_init(&adapter);
    assert_int_equal(rc, 0);
    
    config_adapter_destroy(&adapter);
}

void test_config_adapter_validate(void** state) {
    const char* valid_config = 
        "{\"config_id\":\"CFG001\",\"version\":1,"
        "\"radios\":[{\"name\":\"radio0\",\"channel\":6,\"txpower\":20}],"
        "\"interfaces\":[{\"name\":\"wlan0\",\"ssid\":\"TestSSID\",\"encryption\":\"psk2\",\"key\":\"password\"}]}";
    
    adapter_result_t result = config_adapter_validate(valid_config);
    assert_int_equal(result, ADAPTER_RESULT_SUCCESS);
    
    const char* invalid_config = "{\"missing_config_id\":true}";
    result = config_adapter_validate(invalid_config);
    assert_int_equal(result, ADAPTER_RESULT_VALIDATION_ERROR);
    
    const char* invalid_json = "{invalid}";
    result = config_adapter_validate(invalid_json);
    assert_int_equal(result, ADAPTER_RESULT_PARSE_ERROR);
}

void test_config_adapter_apply(void** state) {
    config_adapter_t adapter;
    config_adapter_init(&adapter);
    
    const char* config = 
        "{\"config_id\":\"CFG001\",\"version\":1,"
        "\"radios\":[],"
        "\"interfaces\":[]}";
    
    adapter_result_t result = config_adapter_apply(&adapter, config);
    
    const char* current_id = config_adapter_get_current_config_id(&adapter);
    assert_string_equal(current_id, "CFG001");
    
    int version = config_adapter_get_current_version(&adapter);
    assert_int_equal(version, 1);
    
    config_adapter_destroy(&adapter);
}

void test_config_adapter_rollback(void** state) {
    config_adapter_t adapter;
    config_adapter_init(&adapter);
    
    adapter_result_t result = config_adapter_rollback(&adapter);
    assert_int_equal(result, ADAPTER_RESULT_ROLLBACK_ERROR);
    
    const char* config_v1 = 
        "{\"config_id\":\"CFG001\",\"version\":1,"
        "\"radios\":[],"
        "\"interfaces\":[]}";
    
    const char* config_v2 = 
        "{\"config_id\":\"CFG002\",\"version\":2,"
        "\"radios\":[],"
        "\"interfaces\":[]}";
    
    config_adapter_apply(&adapter, config_v1);
    config_adapter_apply(&adapter, config_v2);
    
    assert_string_equal(config_adapter_get_current_config_id(&adapter), "CFG002");
    
    result = config_adapter_rollback(&adapter);
    assert_int_equal(result, ADAPTER_RESULT_SUCCESS);
    
    assert_string_equal(config_adapter_get_current_config_id(&adapter), "CFG001");
    
    config_adapter_destroy(&adapter);
}