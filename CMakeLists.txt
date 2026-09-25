cmake_minimum_required(VERSION 3.20)

set(BTH_BOARD "generic_esp32c6" CACHE STRING "Board profile from boards/")
include(cmake/board.cmake)

# Each build directory owns its configuration; targets never share sdkconfig.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "Local SDK configuration")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${BTH_BOARD_DIR}/sdkconfig.defaults")
set(COMPONENTS main)
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bt_home)

if(NOT IDF_VERSION_MAJOR EQUAL 5 OR IDF_VERSION_MINOR LESS 4 OR IDF_VERSION_MINOR GREATER 5)
    message(FATAL_ERROR "This project requires ESP-IDF 5.4 or 5.5. Validated baseline: v5.4.")
endif()
