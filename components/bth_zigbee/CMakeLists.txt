set(zigbee_sources "")
set(zigbee_sdk "")
if(IDF_TARGET STREQUAL "esp32c6")
    set(zigbee_sdk espressif__esp-zigbee-lib)
endif()
if(CONFIG_BTH_ZIGBEE_ENABLED)
    set(zigbee_sources "src/zigbee.cpp" "src/storage.cpp")
endif()
# Dependencies must be available in ESP-IDF's requirements pass, before sdkconfig.
idf_component_register(SRCS ${zigbee_sources} INCLUDE_DIRS "include"
    REQUIRES bth_core bth_platform freertos
    PRIV_REQUIRES ${zigbee_sdk} esp_timer esp_partition nvs_flash log)
if(CONFIG_BTH_ZIGBEE_ENABLED)
    if(NOT CONFIG_ZB_ZED OR NOT CONFIG_ZB_RADIO_NATIVE OR NOT CONFIG_ESP_COEX_SW_COEXIST_ENABLE)
        message(FATAL_ERROR "B-T HOME Zigbee requires ZB_ZED, ZB_RADIO_NATIVE and ESP_COEX_SW_COEXIST_ENABLE in sdkconfig")
    endif()
    target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
endif()
