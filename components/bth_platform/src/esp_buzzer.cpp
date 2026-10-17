#include "bth/platform/esp_buzzer.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t error) noexcept {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("buzzer", "%s", esp_err_to_name(error));
    return Status::hardware_error;
}
}
Status EspBuzzer::init() noexcept {
    if (acquired_) return Status::invalid_state;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_)) return Status::invalid_argument;
    acquired_ = true;
    // Preload silence before enabling the output driver.
    const auto result = gpio_set_level(static_cast<gpio_num_t>(gpio_), active_high_ ? 0 : 1);
    if (result != ESP_OK) return checked(result);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << gpio_;
    config.mode = GPIO_MODE_OUTPUT;
    const auto configured = gpio_config(&config);
    ready_ = configured == ESP_OK;
    return checked(configured);
}
Status EspBuzzer::set(bool on) noexcept {
    if (!ready_) return Status::invalid_state;
    return checked(gpio_set_level(static_cast<gpio_num_t>(gpio_), on == active_high_ ? 1 : 0));
}
void EspBuzzer::deinit() noexcept {
    if (!acquired_) return;
    (void)checked(gpio_set_level(static_cast<gpio_num_t>(gpio_), active_high_ ? 0 : 1));
    acquired_ = ready_ = false;
}
}  // namespace bth
