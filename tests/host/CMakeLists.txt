cmake_minimum_required(VERSION 3.20)
project(bt_home_host_tests LANGUAGES CXX)
enable_testing()

if(MSVC)
    # CMake enables exceptions globally by default; opt in only for the test harness.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_library(bth_core STATIC "${ROOT}/components/bth_core/src/runtime.cpp")
target_include_directories(bth_core PUBLIC "${ROOT}/components/bth_core/include")

add_library(bth_radio STATIC "${ROOT}/components/bth_radio/src/radio_mode.cpp")
target_include_directories(bth_radio PUBLIC "${ROOT}/components/bth_radio/include")
target_link_libraries(bth_radio PUBLIC bth_core)

add_library(bth_heartbeat STATIC "${ROOT}/components/bth_heartbeat/src/heartbeat.cpp")
target_include_directories(bth_heartbeat PUBLIC "${ROOT}/components/bth_heartbeat/include")
target_link_libraries(bth_heartbeat PUBLIC bth_core)

add_library(bth_startup_blink STATIC "${ROOT}/components/bth_startup_blink/src/startup_blink.cpp")
target_include_directories(bth_startup_blink PUBLIC "${ROOT}/components/bth_startup_blink/include")
target_link_libraries(bth_startup_blink PUBLIC bth_core)

add_library(bth_relay STATIC "${ROOT}/components/bth_relay/src/relay.cpp")
target_include_directories(bth_relay PUBLIC "${ROOT}/components/bth_relay/include")
target_link_libraries(bth_relay PUBLIC bth_core)

add_library(bth_access STATIC "${ROOT}/components/bth_access/src/access.cpp")
target_include_directories(bth_access PUBLIC "${ROOT}/components/bth_access/include")
target_link_libraries(bth_access PUBLIC bth_core)

add_library(bth_battery STATIC "${ROOT}/components/bth_battery/src/battery.cpp")
target_include_directories(bth_battery PUBLIC "${ROOT}/components/bth_battery/include")
target_link_libraries(bth_battery PUBLIC bth_core bth_mains bth_dc)

add_library(bth_oled STATIC "${ROOT}/components/bth_oled/src/oled.cpp")
target_include_directories(bth_oled PUBLIC "${ROOT}/components/bth_oled/include")
target_link_libraries(bth_oled PUBLIC bth_core bth_mains bth_battery)

add_library(bth_mains STATIC "${ROOT}/components/bth_mains/src/mains.cpp")
target_include_directories(bth_mains PUBLIC "${ROOT}/components/bth_mains/include")
target_link_libraries(bth_mains PUBLIC bth_core)

add_library(bth_dc STATIC "${ROOT}/components/bth_dc/src/dc.cpp")
target_include_directories(bth_dc PUBLIC "${ROOT}/components/bth_dc/include")
target_link_libraries(bth_dc PUBLIC bth_core)

add_executable(bth_tests test_battery.cpp test_main.cpp test_mains.cpp test_dc.cpp test_oled.cpp test_startup_blink.cpp test_relay.cpp test_access.cpp test_web_voltage.cpp test_zigbee.cpp test_relay_service.cpp
    "${ROOT}/components/bth_platform/src/relay_service.cpp"
    "${ROOT}/components/bth_zigbee/src/storage.cpp" test_zigbee_storage.cpp test_radio.cpp test_matter.cpp)
target_include_directories(bth_tests PRIVATE "${ROOT}/components/bth_matter/include")
target_include_directories(bth_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs")
target_include_directories(bth_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/freertos" "${ROOT}/components/bth_platform/include")
find_package(Threads REQUIRED)
target_link_libraries(bth_tests PRIVATE Threads::Threads)
target_include_directories(bth_tests PRIVATE "${ROOT}/components/bth_web/include" "${ROOT}/components/bth_zigbee/include")
target_link_libraries(bth_tests PRIVATE bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc)
if(MSVC)
    target_compile_options(bth_tests PRIVATE /EHsc)
endif()

foreach(target IN ITEMS bth_battery bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc bth_tests)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# Check portable firmware code without exceptions or RTTI; test helpers may use them.
foreach(target IN ITEMS bth_battery bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c- /GR-)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
    endif()
endforeach()
add_test(NAME module_lifecycle_and_heartbeat COMMAND bth_tests)
