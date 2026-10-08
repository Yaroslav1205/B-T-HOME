#include "bth/platform/boot_button.hpp"
#include "driver/gpio.h"

namespace bth {
Status BootButton::init() noexcept {
    if (acquired_) return Status::invalid_state;
    if (!GPIO_IS_VALID_GPIO(gpio_)) return Status::invalid_argument;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << gpio_;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = active_low_ ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_down_en = active_low_ ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    acquired_ = true;
    return gpio_config(&config) == ESP_OK ? Status::ok : Status::hardware_error;
}
Status BootButton::start() noexcept {
    if (!acquired_ || running_) return Status::invalid_state;
    gesture_ = {}; action_ = ButtonAction::none; running_ = true;
    return Status::ok;
}
Status BootButton::tick(std::uint64_t now) noexcept {
    if (!running_) return Status::invalid_state;
    const auto action = gesture_.update((gpio_get_level(static_cast<gpio_num_t>(gpio_)) == 0) == active_low_, now);
    if (action != ButtonAction::none) action_ = action;
    return Status::ok;
}
void BootButton::deinit() noexcept {
    stop();
    if (acquired_) { (void)gpio_reset_pin(static_cast<gpio_num_t>(gpio_)); acquired_ = false; }
}
}  // namespace bth
