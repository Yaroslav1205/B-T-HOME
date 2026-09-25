idf_component_register()
configure_file("board_config.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/bth/board/config.hpp" @ONLY)
target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
