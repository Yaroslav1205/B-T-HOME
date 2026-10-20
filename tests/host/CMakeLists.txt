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

add_library(bth_outages STATIC "${ROOT}/components/bth_outages/src/outages.cpp")
target_include_directories(bth_outages PUBLIC "${ROOT}/components/bth_outages/include")
target_link_libraries(bth_outages PUBLIC bth_core)
add_executable(bth_outage_tests test_outages.cpp)
target_link_libraries(bth_outage_tests PRIVATE bth_outages)
add_test(NAME outage_parser_and_polling COMMAND bth_outage_tests)
add_executable(bth_outage_settings_tests test_outage_settings.cpp "${ROOT}/components/bth_platform/src/outage_settings.cpp")
target_include_directories(bth_outage_settings_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs" "${CMAKE_CURRENT_LIST_DIR}/freertos" "${ROOT}/components/bth_platform/include")
target_link_libraries(bth_outage_settings_tests PRIVATE bth_outages)
add_test(NAME outage_settings_persistence COMMAND bth_outage_settings_tests)
if(MSVC)
    target_compile_options(bth_outage_tests PRIVATE /EHsc)
    target_compile_options(bth_outage_settings_tests PRIVATE /EHsc)
endif()

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

add_library(bth_oled STATIC "${ROOT}/components/bth_oled/src/oled.cpp" "${ROOT}/components/bth_oled/src/oled_writer.cpp")
target_include_directories(bth_oled PUBLIC "${ROOT}/components/bth_oled/include")
target_link_libraries(bth_oled PUBLIC bth_outages bth_core bth_mains bth_battery)

add_library(bth_mains STATIC "${ROOT}/components/bth_mains/src/mains.cpp")
target_include_directories(bth_mains PUBLIC "${ROOT}/components/bth_mains/include")
target_link_libraries(bth_mains PUBLIC bth_core)

add_library(bth_dc STATIC "${ROOT}/components/bth_dc/src/dc.cpp")
target_include_directories(bth_dc PUBLIC "${ROOT}/components/bth_dc/include")
target_link_libraries(bth_dc PUBLIC bth_core)

add_library(bth_buzzer STATIC "${ROOT}/components/bth_buzzer/src/buzzer.cpp")
target_include_directories(bth_buzzer PUBLIC "${ROOT}/components/bth_buzzer/include")
target_link_libraries(bth_buzzer PUBLIC bth_core bth_mains)

add_executable(bth_tests test_buzzer.cpp test_battery.cpp test_main.cpp test_mains.cpp test_dc.cpp test_oled.cpp test_oled_writer.cpp test_startup_blink.cpp test_relay.cpp test_access.cpp test_web_voltage.cpp test_web_request.cpp test_zigbee.cpp test_relay_service.cpp
    "${ROOT}/components/bth_platform/src/relay_service.cpp"
    "${ROOT}/components/bth_platform/src/esp_gpio_led.cpp" test_gpio_led.cpp
    "${ROOT}/components/bth_zigbee/src/storage.cpp" test_zigbee_storage.cpp test_radio.cpp test_matter.cpp)
target_include_directories(bth_tests PRIVATE "${ROOT}/components/bth_matter/include")
target_include_directories(bth_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs")
target_include_directories(bth_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/freertos" "${ROOT}/components/bth_platform/include")
find_package(Threads REQUIRED)
add_executable(bth_oled_i2c_tests test_oled_i2c.cpp
    "${ROOT}/components/bth_platform/src/esp_oled_i2c.cpp")
target_include_directories(bth_oled_i2c_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/oled_stubs"
    "${CMAKE_CURRENT_LIST_DIR}/idf_stubs" "${CMAKE_CURRENT_LIST_DIR}/freertos"
    "${ROOT}/components/bth_platform/include")
target_link_libraries(bth_oled_i2c_tests PRIVATE bth_oled Threads::Threads)
if(MSVC)
    target_compile_options(bth_oled_i2c_tests PRIVATE /EHsc)
endif()
set_target_properties(bth_oled_i2c_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
add_test(NAME oled_i2c_worker_recovery COMMAND bth_oled_i2c_tests)
set_tests_properties(oled_i2c_worker_recovery PROPERTIES TIMEOUT 10)
target_link_libraries(bth_tests PRIVATE Threads::Threads)
target_include_directories(bth_tests PRIVATE "${ROOT}/components/bth_web/include" "${ROOT}/components/bth_zigbee/include")
target_link_libraries(bth_tests PRIVATE bth_buzzer bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc)
if(MSVC)
    target_compile_options(bth_tests PRIVATE /EHsc)
endif()

add_executable(bth_buzzer_settings_tests test_buzzer_settings.cpp
    "${ROOT}/components/bth_platform/src/buzzer_settings.cpp")
target_include_directories(bth_buzzer_settings_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs"
    "${CMAKE_CURRENT_LIST_DIR}/freertos" "${ROOT}/components/bth_platform/include")
target_link_libraries(bth_buzzer_settings_tests PRIVATE bth_buzzer)
if(MSVC)
    target_compile_options(bth_buzzer_settings_tests PRIVATE /EHsc)
endif()
add_test(NAME buzzer_settings_persistence COMMAND bth_buzzer_settings_tests)

add_executable(bth_battery_settings_tests test_battery_settings.cpp
    "${ROOT}/components/bth_platform/src/battery_settings.cpp")
target_include_directories(bth_battery_settings_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs"
    "${CMAKE_CURRENT_LIST_DIR}/freertos" "${ROOT}/components/bth_platform/include")
target_link_libraries(bth_battery_settings_tests PRIVATE bth_battery)
if(MSVC)
    target_compile_options(bth_battery_settings_tests PRIVATE /EHsc)
endif()
add_test(NAME battery_settings_persistence COMMAND bth_battery_settings_tests)

add_executable(bth_access_store_tests test_access_store.cpp "${ROOT}/components/bth_platform/src/access_store.cpp")
target_include_directories(bth_access_store_tests PRIVATE "${ROOT}/components/bth_platform/include" idf_stubs)
target_link_libraries(bth_access_store_tests PRIVATE bth_access bth_radio)
if(MSVC)
    target_compile_options(bth_access_store_tests PRIVATE /EHsc)
endif()
add_test(NAME access_migration_and_radio_persistence COMMAND bth_access_store_tests)

add_executable(bth_wifi_ap_tests test_wifi_ap.cpp "${ROOT}/components/bth_platform/src/wifi_ap.cpp")
target_include_directories(bth_wifi_ap_tests PRIVATE "${ROOT}/components/bth_platform/include" idf_stubs)
target_compile_definitions(bth_wifi_ap_tests PRIVATE CONFIG_ESP_WIFI_SOFTAP_SUPPORT=1)
target_link_libraries(bth_wifi_ap_tests PRIVATE bth_access bth_radio)
if(MSVC)
    target_compile_options(bth_wifi_ap_tests PRIVATE /EHsc)
endif()
add_test(NAME wifi_ap_dhcp_and_cleanup COMMAND bth_wifi_ap_tests)

foreach(target IN ITEMS bth_outages bth_outage_tests bth_outage_settings_tests bth_battery_settings_tests bth_wifi_ap_tests bth_access_store_tests bth_buzzer_settings_tests bth_buzzer bth_battery bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc bth_tests)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

# Check portable firmware code without exceptions or RTTI; test helpers may use them.
foreach(target IN ITEMS bth_outages bth_buzzer bth_battery bth_radio bth_core bth_heartbeat bth_startup_blink bth_relay bth_access bth_oled bth_mains bth_dc)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHs-c- /GR-)
    else()
        target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
    endif()
endforeach()
add_test(NAME module_lifecycle_and_heartbeat COMMAND bth_tests)
set_tests_properties(module_lifecycle_and_heartbeat PROPERTIES TIMEOUT 30)

add_executable(bth_relay_store_tests test_relay_store.cpp "${ROOT}/components/bth_platform/src/nvs_storage.cpp")
target_include_directories(bth_relay_store_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/idf_stubs" "${ROOT}/components/bth_platform/include")
target_link_libraries(bth_relay_store_tests PRIVATE bth_relay)
set_target_properties(bth_relay_store_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
add_test(NAME relay_mode_persistence COMMAND bth_relay_store_tests)

add_library(bth_ota "${ROOT}/components/bth_ota/src/update.cpp")
target_include_directories(bth_ota PUBLIC "${ROOT}/components/bth_ota/include")
add_executable(bth_ota_tests test_ota.cpp)
target_link_libraries(bth_ota_tests PRIVATE bth_ota)
set_target_properties(bth_ota bth_ota_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(bth_ota PRIVATE /W4 /WX /EHs-c- /GR-)
else()
    target_compile_options(bth_ota PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME ota_transaction COMMAND bth_ota_tests)
