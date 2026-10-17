#include "bth/platform/buzzer_settings.hpp"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t error) noexcept {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("buzzer", "Settings: %s", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}
Status NvsBuzzerSettings::init() noexcept {
    if (handle_ || queue_) return Status::invalid_state;
    auto error = nvs_open("bth_buzzer", NVS_READWRITE, &handle_);
    if (error != ESP_OK) return checked(error);
    std::uint32_t seconds = buzzer_default_interval_seconds;
    error = nvs_get_u32(handle_, "interval_s", &seconds);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return checked(error);
    if (seconds > buzzer_max_interval_seconds) return Status::invalid_argument;
    queue_ = xQueueCreate(1, sizeof(seconds));
    if (!queue_) return Status::no_memory;
    xQueueOverwrite(queue_, &seconds);
    writable_ = true;
    return Status::ok;
}
std::uint32_t NvsBuzzerSettings::interval_seconds() const noexcept {
    std::uint32_t seconds = 0;
    if (queue_) xQueuePeek(queue_, &seconds, 0);
    return seconds;
}
Status NvsBuzzerSettings::update_interval(std::uint32_t seconds) noexcept {
    if (!writable_ || !handle_ || !queue_) return Status::invalid_state;
    if (seconds > buzzer_max_interval_seconds) return Status::invalid_argument;
    if (seconds == interval_seconds()) return Status::ok;
    auto error = nvs_set_u32(handle_, "interval_s", seconds);
    if (error == ESP_OK) error = nvs_commit(handle_);
    if (error != ESP_OK) {
        // Durability is uncertain after failed commit. Keep the published value
        // and reject further writes until reinitialization; never erase NVS.
        writable_ = false;
        return checked(error);
    }
    xQueueOverwrite(queue_, &seconds);
    return Status::ok;
}
void NvsBuzzerSettings::deinit() noexcept {
    writable_ = false;
    if (queue_) { vQueueDelete(queue_); queue_ = nullptr; }
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
}  // namespace bth
