#pragma once
#include <cstdint>
#include "esp_err.h"
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 2;
// The fake accepts only the test pin; it does not claim capabilities of a chip.
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) == 2)
struct gpio_config_t {
    std::uint64_t pin_bit_mask = 0;
    int mode = 0;
};
esp_err_t gpio_set_level(gpio_num_t pin, std::uint32_t level);
esp_err_t gpio_config(const gpio_config_t* config);
