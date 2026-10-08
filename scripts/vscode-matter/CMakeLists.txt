cmake_minimum_required(VERSION 3.16)
project(bth_matter_windows_bridge NONE)

# The extension runs CMake/Ninja on Windows; actual firmware compilation stays
# in the pinned Linux SDK. Flash and Monitor use the exported Windows artifacts.
get_filename_component(BTH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_custom_target(matter ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${BTH_ROOT}/scripts/matter.ps1" -Action build
    WORKING_DIRECTORY "${BTH_ROOT}"
    USES_TERMINAL
    VERBATIM)
