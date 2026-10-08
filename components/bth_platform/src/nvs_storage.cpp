#include "bth/platform/nvs_storage.hpp"
#include "nvs_flash.h"
#include "esp_log.h"
namespace bth {
namespace {
Status check(esp_err_t error) noexcept {
    if (error == ESP_OK) { return Status::ok; }
    ESP_LOGE("storage", "%s (NVS will not be erased)", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}
Status NvsStorage::init() noexcept {
    const auto result = nvs_flash_init();
    initialized_ = result == ESP_OK;
    return check(result);
}
void NvsStorage::deinit() noexcept {
    if (initialized_) { (void)check(nvs_flash_deinit()); }
    initialized_ = false;
}
Status NvsRelayStore::init() noexcept {
    if (handle_) { return Status::invalid_state; }
    return check(nvs_open("bth_relay", NVS_READWRITE, &handle_));
}
Status NvsRelayStore::load(bool& on) noexcept {
    if (!handle_) { return Status::invalid_state; }
    uint8_t value = 0;
    const auto result = nvs_get_u8(handle_, "on", &value);
    if (result == ESP_ERR_NVS_NOT_FOUND) { on = false; return Status::ok; }
    if (result != ESP_OK) { return check(result); }
    if (value > 1) { return Status::invalid_argument; }
    on = value != 0;
    return Status::ok;
}
Status NvsRelayStore::save(bool on) noexcept {
    if (!handle_) { return Status::invalid_state; }
    const auto result = nvs_set_u8(handle_, "on", on ? 1 : 0);
    if (result != ESP_OK) { return check(result); }
    return check(nvs_commit(handle_));
}
void NvsRelayStore::deinit() noexcept {
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
}  // namespace bth
