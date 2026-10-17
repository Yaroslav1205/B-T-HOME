idf_component_register()
set(BTH_BOARD_HAS_LED_CONFIG OFF)
if(EXISTS "${BTH_BOARD_DIR}/led_config.hpp")
    set(BTH_BOARD_HAS_LED_CONFIG ON)
    configure_file("${BTH_BOARD_DIR}/led_config.hpp"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/bth/board/led_config.hpp" COPYONLY)
endif()
configure_file("board_config.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/bth/board/config.hpp" @ONLY)
target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
