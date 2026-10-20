#include "bth/platform/battery_settings.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(c) do { if (!(c)) { std::cerr << "battery settings:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
struct TestQueue { std::uint32_t value = 0; };
namespace {
bool opened = false, found = false, fail_queue = false;
unsigned queues = 0, writes = 0;
std::uint32_t persisted = 58, staged = 58;
esp_err_t load_result = ESP_OK, save_result = ESP_OK, commit_result = ESP_OK;
}
const char* esp_err_to_name(esp_err_t) { return "test_error"; }
esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* handle) {
    CHECK(std::strcmp(name, "bth_battery") == 0 && mode == NVS_READWRITE && !opened);
    opened = true; *handle = 1; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { CHECK(handle == 1 && opened); opened = false; }
esp_err_t nvs_get_u32(nvs_handle_t handle, const char* key, std::uint32_t* value) {
    CHECK(handle == 1 && opened && std::strcmp(key, "discharge_ma") == 0);
    if (load_result != ESP_OK) return load_result;
    if (!found) return ESP_ERR_NVS_NOT_FOUND;
    *value = persisted; return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t handle, const char* key, std::uint32_t value) {
    CHECK(handle == 1 && opened && std::strcmp(key, "discharge_ma") == 0);
    staged = value; ++writes; return save_result;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    CHECK(handle == 1 && opened);
    if (commit_result == ESP_OK) { persisted = staged; found = true; }
    return commit_result;
}
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size) {
    CHECK(capacity == 1 && size == sizeof(std::uint32_t));
    if (fail_queue) return nullptr;
    ++queues; return new TestQueue;
}
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void* value) {
    queue->value = *static_cast<const std::uint32_t*>(value); return pdPASS;
}
BaseType_t xQueuePeek(QueueHandle_t queue, void* value, TickType_t wait) {
    CHECK(wait == 0); *static_cast<std::uint32_t*>(value) = queue->value; return pdPASS;
}
void vQueueDelete(QueueHandle_t queue) { CHECK(queues == 1); --queues; delete queue; }
int main() {
    using bth::Status;
    bth::NvsBatterySettings settings;
    CHECK(settings.start() == Status::invalid_state);
    CHECK(settings.update_discharge(1) == Status::invalid_state);
    CHECK(settings.init() == Status::ok && settings.start() == Status::ok);
    CHECK(settings.init() == Status::invalid_state);
    CHECK(settings.discharge_ma() == 58 && writes == 0);
    CHECK(settings.update_discharge(58) == Status::ok && writes == 0);
    CHECK(settings.update_discharge(100001) == Status::invalid_argument && writes == 0);
    CHECK(settings.update_discharge(1) == Status::ok && persisted == 1 && settings.discharge_ma() == 1);
    CHECK(settings.update_discharge(0) == Status::invalid_argument);
    settings.deinit(); settings.deinit();
    CHECK(!opened && queues == 0);
    CHECK(settings.init() == Status::ok && settings.discharge_ma() == 1);
    CHECK(settings.update_discharge(100000) == Status::ok && persisted == 100000);
    commit_result = ESP_FAIL;
    CHECK(settings.update_discharge(5) == Status::hardware_error);
    CHECK(settings.discharge_ma() == 100000 && persisted == 100000);
    commit_result = ESP_OK;
    CHECK(settings.update_discharge(5) == Status::invalid_state);
    settings.deinit();
    CHECK(settings.init() == Status::ok && settings.discharge_ma() == 100000);
    save_result = ESP_FAIL;
    CHECK(settings.update_discharge(1) == Status::hardware_error && settings.discharge_ma() == 100000);
    settings.deinit(); save_result = ESP_OK;
    CHECK(settings.init() == Status::ok);
    CHECK(settings.update_discharge(5) == Status::ok && settings.discharge_ma() == 5);
    settings.deinit();
    persisted = 100001;
    CHECK(settings.init() == Status::invalid_argument); settings.deinit();
    persisted = 58; load_result = ESP_FAIL;
    CHECK(settings.init() == Status::hardware_error); settings.deinit();
    load_result = ESP_OK; fail_queue = true;
    CHECK(settings.init() == Status::no_memory); settings.deinit();
    CHECK(!opened && queues == 0);
    std::cout << "PASS: battery settings persistence, bounds, commit failures and cleanup\n";
}
