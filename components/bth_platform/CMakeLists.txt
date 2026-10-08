idf_component_register(
    SRCS "src/esp_mains.cpp" "src/esp_heartbeat_sink.cpp" "src/esp_oled_i2c.cpp" "src/esp_rgb_led.cpp"
         "src/battery_store.cpp" "src/nvs_storage.cpp" "src/esp_relay.cpp" "src/relay_service.cpp" "src/wifi_ap.cpp" "src/access_store.cpp" "src/boot_button.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bth_battery bth_radio bth_dc bth_mains esp_adc bth_oled esp_driver_i2c bth_heartbeat bth_startup_blink bth_relay bth_access esp_driver_rmt nvs_flash freertos esp_netif
    PRIV_REQUIRES esp_system log esp_driver_gpio esp_wifi esp_event esp_hw_support mbedtls
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
