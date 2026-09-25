cmake_minimum_required(VERSION 3.20)
project(bt_home_host_tests LANGUAGES CXX)
enable_testing()

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(bth_tests
    test_main.cpp
    "${ROOT}/components/bth_core/src/runtime.cpp"
    "${ROOT}/components/bth_heartbeat/src/heartbeat.cpp"
)
target_include_directories(bth_tests PRIVATE
    "${ROOT}/components/bth_core/include"
    "${ROOT}/components/bth_heartbeat/include"
)
set_target_properties(bth_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(MSVC)
    target_compile_options(bth_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(bth_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME module_lifecycle_and_heartbeat COMMAND bth_tests)
