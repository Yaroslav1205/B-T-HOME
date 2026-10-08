#include "bth/zigbee/storage.hpp"
#include "nvs_flash.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define CHECK_ZS(c) do { if (!(c)) { std::cerr << "zigbee storage:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while(false)
namespace {
std::vector<int> calls;
esp_err_t init_error = ESP_OK, open_error = ESP_OK;
bool partition_ready = false, handle_open = false;
}
esp_err_t nvs_flash_init_partition(const char* name) {
    CHECK_ZS(std::strcmp(name,"zb_storage") == 0 && !partition_ready);
    calls.push_back(1);
    partition_ready = init_error == ESP_OK;
    return init_error;
}
esp_err_t nvs_open_from_partition(const char* name, const char* space, int mode, nvs_handle_t* handle) {
    CHECK_ZS(partition_ready && std::strcmp(name,"zb_storage") == 0);
    CHECK_ZS(std::strcmp(space,"zigbee") == 0 && mode == NVS_READWRITE && !handle_open);
    calls.push_back(2);
    if (open_error == ESP_OK) { *handle = 17; handle_open = true; }
    return open_error;
}
void nvs_close(nvs_handle_t handle) {
    CHECK_ZS(handle == 17 && handle_open);
    calls.push_back(3); handle_open = false;
}
esp_err_t nvs_flash_deinit_partition(const char* name) {
    CHECK_ZS(partition_ready && !handle_open && std::strcmp(name,"zb_storage") == 0);
    calls.push_back(4); partition_ready = false; return ESP_OK;
}
const char* esp_err_to_name(esp_err_t) { return "test_error"; }

void test_zigbee_storage() {
    bth::ZigbeeStorage storage;
    storage.deinit(); CHECK_ZS(calls.empty());
    CHECK_ZS(storage.init() == bth::Status::ok);
    CHECK_ZS((calls == std::vector<int>{1,2,3}));
    CHECK_ZS(storage.init() == bth::Status::invalid_state);
    storage.deinit(); storage.deinit();
    CHECK_ZS((calls == std::vector<int>{1,2,3,4}));
    calls.clear(); init_error = ESP_FAIL;
    CHECK_ZS(storage.init() == bth::Status::hardware_error);
    storage.deinit(); CHECK_ZS((calls == std::vector<int>{1}));
    calls.clear(); init_error = ESP_OK; open_error = ESP_ERR_NO_MEM;
    CHECK_ZS(storage.init() == bth::Status::no_memory);
    storage.deinit(); storage.deinit();
    CHECK_ZS((calls == std::vector<int>{1,2,4}));
    calls.clear(); open_error = ESP_FAIL;
    CHECK_ZS(storage.init() == bth::Status::hardware_error);
    storage.deinit(); CHECK_ZS((calls == std::vector<int>{1,2,4}));
    calls.clear(); open_error = ESP_OK;
    CHECK_ZS(storage.init() == bth::Status::ok);
    storage.deinit(); CHECK_ZS((calls == std::vector<int>{1,2,3,4}));
}
