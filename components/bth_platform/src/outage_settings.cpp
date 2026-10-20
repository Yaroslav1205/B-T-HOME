#include "bth/platform/outage_settings.hpp"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t error) noexcept {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("outages", "Settings: %s", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}
Status NvsOutageSettings::init() noexcept {
    if (handle_ || queue_) return Status::invalid_state;
    auto error = nvs_open("bth_outages", NVS_READWRITE, &handle_);
    if (error != ESP_OK) return checked(error);
    std::uint32_t selection = outage_default_group;
    error = nvs_get_u32(handle_, "group", &selection);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) return checked(error);
    if (selection > outage_max_group) return Status::invalid_argument;
    queue_ = xQueueCreate(1, sizeof(selection));
    if (!queue_) return Status::no_memory;
    xQueueOverwrite(queue_, &selection);
    writable_ = true;
    return Status::ok;
}
std::uint32_t NvsOutageSettings::group() const noexcept {
    std::uint32_t selection = 0;
    if (queue_) xQueuePeek(queue_, &selection, 0);
    return selection;
}
Status NvsOutageSettings::update_group(std::uint32_t selection) noexcept {
    if (!writable_ || !handle_ || !queue_) return Status::invalid_state;
    if (selection > outage_max_group) return Status::invalid_argument;
    if (selection == group()) return Status::ok;
    auto error = nvs_set_u32(handle_, "group", selection);
    if (error == ESP_OK) error = nvs_commit(handle_);
    if (error != ESP_OK) {
        // Durability is uncertain after failed commit. Keep the published value
        // and reject further writes until reinitialization; never erase NVS.
        writable_ = false;
        return checked(error);
    }
    xQueueOverwrite(queue_, &selection);
    return Status::ok;
}
void NvsOutageSettings::deinit() noexcept {
    writable_ = false;
    if (queue_) { vQueueDelete(queue_); queue_ = nullptr; }
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
}  // namespace bth
