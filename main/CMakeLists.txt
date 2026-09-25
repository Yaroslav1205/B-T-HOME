idf_component_register(
    SRCS "app_main.cpp" "application.cpp"
    PRIV_REQUIRES bth_core bth_board bth_platform bth_heartbeat esp_timer freertos log
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
