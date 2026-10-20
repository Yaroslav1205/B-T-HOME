cmake_minimum_required(VERSION 3.20)

# Both the extension and idf.py reconfigure from the workspace root.
option(BTH_WINDOWS_NATIVE_BRIDGE "Delegate Windows IDE builds to the native wrapper"
    "$ENV{BTH_WINDOWS_NATIVE_BRIDGE}")
if(WIN32 AND BTH_WINDOWS_NATIVE_BRIDGE)
    include(scripts/vscode-native/CMakeLists.txt)
    return()
endif()

option(BTH_WINDOWS_MATTER_BRIDGE "Delegate Windows Matter builds to WSL"
    "$ENV{BTH_WINDOWS_MATTER_BRIDGE}")
if(WIN32 AND BTH_WINDOWS_MATTER_BRIDGE)
    include(scripts/vscode-matter/CMakeLists.txt)
    return()
endif()

set(BTH_BOARD "esp32c6_rgb8" CACHE STRING "Board profile from boards/")
include(cmake/board.cmake)

# Each build directory owns its configuration; targets never share sdkconfig.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "Local SDK configuration")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${BTH_BOARD_DIR}/sdkconfig.defaults")
set(COMPONENTS main)
set(PROJECT_VER "0.2.0")
option(BTH_WITH_MATTER "Build Matter over Wi-Fi using ESP-Matter SDK" OFF)
if(BTH_WITH_MATTER)
    if(WIN32 OR NOT DEFINED ENV{ESP_MATTER_PATH})
        message(FATAL_ERROR "Matter requires Linux/WSL and ESP_MATTER_PATH. See docs/MATTER.md")
    endif()
    set(ESP_MATTER_PATH "$ENV{ESP_MATTER_PATH}")
    set(MATTER_SDK_PATH "${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.matter.defaults")
    set(EXTRA_COMPONENT_DIRS "${ESP_MATTER_PATH}/components" "${MATTER_SDK_PATH}/config/esp32/components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bt_home)
if(BTH_BOARD STREQUAL "esp32c6_rgb8" AND NOT BTH_WITH_MATTER)
    if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE OR
       NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "boards/esp32c6_rgb8/partitions.csv")
        message(FATAL_ERROR "OTA requires rollback and boards/esp32c6_rgb8/partitions.csv. Update the existing build/<profile>/sdkconfig; defaults do not replace it. See docs/OTA.md")
    endif()
endif()
if(CONFIG_BTH_MATTER_ENABLED AND NOT BTH_WITH_MATTER)
    message(FATAL_ERROR "CONFIG_BTH_MATTER_ENABLED requires -DBTH_WITH_MATTER=ON and the Linux/WSL Matter SDK")
endif()
if(BTH_WITH_MATTER)
    if(NOT CONFIG_BTH_MATTER_ENABLED OR CONFIG_BTH_ZIGBEE_ENABLED)
        message(FATAL_ERROR "Matter build requires CONFIG_BTH_MATTER_ENABLED=y and CONFIG_BTH_ZIGBEE_ENABLED=n in sdkconfig.")
    endif()
    idf_build_set_property(CXX_COMPILE_OPTIONS "-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
    if(NOT DEFINED CONFIG_SUPPORT_ELECTRICAL_POWER_MEASUREMENT_CLUSTER OR
       NOT CONFIG_SUPPORT_ELECTRICAL_POWER_MEASUREMENT_CLUSTER OR
       NOT DEFINED CONFIG_SUPPORT_POWER_TOPOLOGY_CLUSTER OR
       NOT CONFIG_SUPPORT_POWER_TOPOLOGY_CLUSTER OR
       NOT DEFINED CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT OR
    CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT LESS 7 OR
       NOT CONFIG_SUPPORT_FIXED_LABEL_CLUSTER)
        message(FATAL_ERROR
            "Matter sdkconfig requires electrical power measurement, power topology, fixed label and at least 7 dynamic endpoints. Update the Linux build sdkconfig to match sdkconfig.matter.defaults, then rebuild.")
    endif()
endif()

if(NOT IDF_VERSION_MAJOR EQUAL 5 OR IDF_VERSION_MINOR LESS 4 OR IDF_VERSION_MINOR GREATER 5)
    message(FATAL_ERROR "This project requires ESP-IDF 5.4 or 5.5. Validated baseline: v5.4.")
endif()
