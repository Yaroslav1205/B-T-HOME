idf_component_register(
    SRCS "src/relay_web.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bth_zigbee bth_core bth_access bth_platform bth_mains bth_dc esp_http_server freertos
    PRIV_REQUIRES json esp_timer esp_hw_support log
    EMBED_TXTFILES "web/index.html"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
