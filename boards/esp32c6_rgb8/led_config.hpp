#pragma once

// Select BTH_LED_VARIANT_GPIO for the ordinary LED, or BTH_LED_VARIANT_RGB
// for the original WS2812 on rgb_led_gpio from board.json.
#define BTH_LED_VARIANT BTH_LED_VARIANT_GPIO
#define BTH_GPIO_LED_PIN 2

// Required for the GPIO variant: 1 = lights at HIGH, 0 = lights at LOW.
#define BTH_GPIO_LED_ACTIVE_HIGH 1
