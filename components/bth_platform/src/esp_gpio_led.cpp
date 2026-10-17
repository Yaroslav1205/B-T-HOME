#include "bth/platform/esp_gpio_led.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t result) noexcept {
    if (result == ESP_OK) return Status::ok;
    ESP_LOGE("gpio_led", "%s", esp_err_to_name(result));
    return Status::hardware_error;
}
}
Status EspGpioLed::init() noexcept {
    if (acquired_) return Status::invalid_state;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_)) return Status::invalid_argument;
    acquired_ = true;
    // Preload OFF before enabling the output, for either electrical polarity.
    auto result = gpio_set_level(static_cast<gpio_num_t>(gpio_), active_high_ ? 0 : 1);
    if (result != ESP_OK) return checked(result);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << gpio_;
    config.mode = GPIO_MODE_OUTPUT;
    result = gpio_config(&config);
    ready_ = result == ESP_OK;
    return checked(result);
}
Status EspGpioLed::set(bool on) noexcept {
    if (!ready_) return Status::invalid_state;
    return checked(gpio_set_level(static_cast<gpio_num_t>(gpio_), on == active_high_ ? 1 : 0));
}
void EspGpioLed::deinit() noexcept {
    if (!acquired_) return;
    // Leave a defined OFF level; floating an active-low LED may light it again.
    (void)checked(gpio_set_level(static_cast<gpio_num_t>(gpio_), active_high_ ? 0 : 1));
    ready_ = acquired_ = false;
}
}  // namespace bth
