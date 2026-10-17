cmake_minimum_required(VERSION 3.20)
project(bth_native_windows_bridge NONE)

# Keep the IDE cache in the physical workspace, separate from the SUBST cache.
# The wrapper owns the temporary drive for the entire native build.
get_filename_component(BTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(BTH_NATIVE_BUILD "${BTH_ROOT}/build/esp32c6_rgb8")
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "esp32c6")
    message(FATAL_ERROR "The IDE bridge uses board esp32c6_rgb8. Select esp32c6 or use scripts/idf.ps1 -Board <profile>.")
endif()
set(IDF_TARGET esp32c6 CACHE STRING "Target of the selected board" FORCE)
set(SDKCONFIG "${BTH_NATIVE_BUILD}/sdkconfig" CACHE FILEPATH "Shared native SDK configuration" FORCE)

# Configure also prepares menuconfig and IntelliSense before the first build.
execute_process(COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${BTH_ROOT}/scripts/idf.ps1" -Action reconfigure
    WORKING_DIRECTORY "${BTH_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
file(STRINGS "${BTH_NATIVE_BUILD}/CMakeCache.txt" BTH_PYTHON_ENTRY REGEX "^PYTHON:[^=]*=")
string(REGEX REPLACE "^PYTHON:[^=]*=" "" BTH_IDF_PYTHON "${BTH_PYTHON_ENTRY}")
execute_process(COMMAND "${BTH_IDF_PYTHON}" "${BTH_ROOT}/scripts/vscode-native/export.py"
    --destination "${CMAKE_BINARY_DIR}" --configuration-only
    COMMAND_ERROR_IS_FATAL ANY)
file(READ "${CMAKE_BINARY_DIR}/project_description.json" BTH_DESCRIPTION)
string(JSON BTH_IDF_PATH GET "${BTH_DESCRIPTION}" idf_path)

add_custom_target(firmware ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${BTH_ROOT}/scripts/idf.ps1" -Action build -IdeBuildPath "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${BTH_ROOT}"
    USES_TERMINAL
    VERBATIM)

# Use physical paths for the long-lived editor: it must not hold the SUBST drive
# or prevent Build while its window is open. Save to the authoritative sdkconfig.
add_custom_target(confserver
    COMMAND "${BTH_IDF_PYTHON}" -m kconfserver
        --env-file "${CMAKE_BINARY_DIR}/config.env"
        --kconfig "${BTH_IDF_PATH}/Kconfig"
        --sdkconfig-rename "${BTH_IDF_PATH}/sdkconfig.rename"
        --config "${SDKCONFIG}"
    WORKING_DIRECTORY "${BTH_ROOT}"
    USES_TERMINAL VERBATIM)

foreach(action menuconfig size size-components size-files)
    add_custom_target(${action}
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${BTH_ROOT}/scripts/idf.ps1" -Action ${action}
        WORKING_DIRECTORY "${BTH_ROOT}"
        USES_TERMINAL VERBATIM)
endforeach()

# Ninja Clean removes native objects too, but preserves sdkconfig/calibration.
# The extension's Full Clean uses the matching idf.extraCleanPaths setting.
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${BTH_NATIVE_BUILD}/CMakeCache.txt" "${BTH_NATIVE_BUILD}/CMakeFiles"
    "${BTH_NATIVE_BUILD}/esp-idf" "${BTH_NATIVE_BUILD}/bootloader"
    "${BTH_NATIVE_BUILD}/bootloader-prefix"
    "${BTH_NATIVE_BUILD}/bt_home.elf" "${BTH_NATIVE_BUILD}/bt_home.bin"
    "${BTH_NATIVE_BUILD}/bt_home.map")
