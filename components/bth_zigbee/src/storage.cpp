#include "bth/zigbee/storage.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

namespace bth {
Status ZigbeeStorage::init() noexcept {
    if (initialized_) return Status::invalid_state;
    auto result = nvs_flash_init_partition("zb_storage");
    if (result == ESP_OK) {
        initialized_ = true;
        // SDK 2.0.4 aborts if opening this namespace fails. Check it before SDK init.
        nvs_handle_t handle = 0;
        result = nvs_open_from_partition("zb_storage", "zigbee", NVS_READWRITE, &handle);
        if (result == ESP_OK) nvs_close(handle);
    }
    if (result != ESP_OK) {
        ESP_LOGE("zigbee", "Storage init: %s (NVS will not be erased)", esp_err_to_name(result));
        return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
    }
    ESP_LOGI("zigbee", "Storage ready: zb_storage");
    return Status::ok;
}
void ZigbeeStorage::deinit() noexcept {
    if (!initialized_) return;
    const auto result = nvs_flash_deinit_partition("zb_storage");
    if (result != ESP_OK) ESP_LOGE("zigbee", "Storage deinit: %s", esp_err_to_name(result));
    initialized_ = false;
}
}  // namespace bth
