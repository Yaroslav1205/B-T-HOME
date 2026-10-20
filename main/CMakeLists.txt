idf_component_register(
    SRCS "app_main.cpp" "application.cpp"
    PRIV_REQUIRES esp_system bth_outages bth_buzzer bth_battery bth_matter bth_radio bth_zigbee bth_dc bth_mains bth_oled bth_core bth_board bth_platform bth_heartbeat bth_startup_blink bth_relay bth_access bth_web esp_timer freertos log esp_hw_support
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
