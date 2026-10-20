#pragma once
#include "esp_err.h"
using gpio_num_t = int;
// Confirmed pins of the OLED profile, only for this adapter test.
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) == 23 || (pin) == 15)
