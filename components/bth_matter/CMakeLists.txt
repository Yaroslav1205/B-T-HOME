# IDF's early dependency scan does not inherit project-local CMake options.
# The exported SDK environment is also visible during that scan.
if(DEFINED ENV{ESP_MATTER_PATH} AND NOT WIN32)
    # ESP-Matter 1.4.2's CHIP library omits the static label provider implementation.
    idf_component_register(SRCS "src/matter.cpp" "src/measurement_access.cpp"
        "$ENV{ESP_MATTER_PATH}/connectedhomeip/connectedhomeip/src/platform/ESP32/StaticESP32DeviceInfoProvider.cpp"
        "$ENV{ESP_MATTER_PATH}/connectedhomeip/connectedhomeip/src/platform/ESP32/ESP32DeviceInfoProvider.cpp"
        INCLUDE_DIRS "include"
        REQUIRES bth_core bth_platform esp_matter
        # ESP-Matter's Insights dependency links this target after IDF's scan.
        PRIV_REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash log esp_http_client)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
else()
    idf_component_register(INCLUDE_DIRS "include" REQUIRES bth_core)
endif()
