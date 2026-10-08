#include "bth/platform/esp_relay.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
namespace bth {
namespace {
Status check(esp_err_t error) noexcept {
    if (error == ESP_OK) { return Status::ok; }
    ESP_LOGE("relay_gpio", "%s", esp_err_to_name(error));
    return Status::hardware_error;
}
}
Status EspRelay::init() noexcept {
    if (acquired_) { return Status::invalid_state; }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_)) { return Status::invalid_argument; }
    const auto pin = static_cast<gpio_num_t>(gpio_);
    acquired_ = true;
    // Preload OFF before enabling the output driver to avoid an activation pulse.
    auto result = gpio_set_level(pin, active_high_ ? 0 : 1);
    if (result != ESP_OK) { return check(result); }
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << gpio_;
    config.mode = GPIO_MODE_OUTPUT;
    return check(gpio_config(&config));
}
Status EspRelay::set(bool on) noexcept {
    if (!acquired_) { return Status::invalid_state; }
    return check(gpio_set_level(static_cast<gpio_num_t>(gpio_), on == active_high_ ? 1 : 0));
}
void EspRelay::deinit() noexcept {
    if (acquired_) {
        (void)set(false);
        // Keep the defined OFF drive until reset; floating an active-low relay is unsafe.
        acquired_ = false;
    }
}
}  // namespace bth
