#include "bth/platform/battery_store.hpp"
#include "esp_log.h"
namespace bth {
namespace {
Status check(esp_err_t error) noexcept {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("battery", "NVS: %s; estimate was not saved", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}
Status NvsBatteryStore::init() noexcept {
    if (handle_) return Status::invalid_state;
    return check(nvs_open("bth_battery", NVS_READWRITE, &handle_));
}
Status NvsBatteryStore::load(std::uint64_t& charge, bool& found) noexcept {
    if (!handle_) return Status::invalid_state;
    const auto result = nvs_get_u64(handle_, "charge_v1", &charge);
    found = result == ESP_OK;
    if (result == ESP_ERR_NVS_NOT_FOUND) return Status::ok;
    return check(result);
}
Status NvsBatteryStore::save(std::uint64_t charge) noexcept {
    if (!handle_) return Status::invalid_state;
    const auto result = nvs_set_u64(handle_, "charge_v1", charge);
    return result == ESP_OK ? check(nvs_commit(handle_)) : check(result);
}
void NvsBatteryStore::deinit() noexcept {
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
} // namespace bth
