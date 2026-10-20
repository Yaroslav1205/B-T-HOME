#include "bth/platform/battery_settings.hpp"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t error) noexcept {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("battery", "Settings: %s", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}
Status NvsBatterySettings::init() noexcept {
    if (handle_ || queue_) return Status::invalid_state;
    auto error = nvs_open("bth_battery", NVS_READWRITE, &handle_);
    if (error != ESP_OK) return checked(error);
    std::uint32_t milliamps = battery_default_discharge_ma;
    error = nvs_get_u32(handle_, "discharge_ma", &milliamps);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return checked(error);
    if (!milliamps || milliamps > battery_max_discharge_ma) return Status::invalid_argument;
    queue_ = xQueueCreate(1, sizeof(milliamps));
    if (!queue_) return Status::no_memory;
    xQueueOverwrite(queue_, &milliamps);
    writable_ = true;
    return Status::ok;
}
std::uint32_t NvsBatterySettings::discharge_ma() const noexcept {
    std::uint32_t milliamps = 0;
    if (queue_) xQueuePeek(queue_, &milliamps, 0);
    return milliamps;
}
Status NvsBatterySettings::update_discharge(std::uint32_t milliamps) noexcept {
    if (!writable_ || !handle_ || !queue_) return Status::invalid_state;
    if (!milliamps || milliamps > battery_max_discharge_ma) return Status::invalid_argument;
    if (milliamps == discharge_ma()) return Status::ok;
    auto error = nvs_set_u32(handle_, "discharge_ma", milliamps);
    if (error == ESP_OK) error = nvs_commit(handle_);
    if (error != ESP_OK) {
        // Durability is uncertain after failed commit. Keep the published value
        // and reject further writes until reinitialization; never erase NVS.
        writable_ = false;
        return checked(error);
    }
    xQueueOverwrite(queue_, &milliamps);
    return Status::ok;
}
void NvsBatterySettings::deinit() noexcept {
    writable_ = false;
    if (queue_) { vQueueDelete(queue_); queue_ = nullptr; }
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
}  // namespace bth
