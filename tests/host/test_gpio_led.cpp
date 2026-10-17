#include "bth/platform/esp_gpio_led.hpp"
#include "driver/gpio.h"
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK_LED(c) do { if (!(c)) { std::cerr << "GPIO LED:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
namespace {
std::vector<int> calls;
bool fail_level = false, fail_config = false;
}
esp_err_t gpio_set_level(gpio_num_t pin, std::uint32_t level) {
    CHECK_LED(pin == 2 && level <= 1);
    calls.push_back(static_cast<int>(level));
    return fail_level ? ESP_FAIL : ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t* config) {
    CHECK_LED(config->pin_bit_mask == (1ULL << 2) && config->mode == GPIO_MODE_OUTPUT);
    calls.push_back(2);
    return fail_config ? ESP_FAIL : ESP_OK;
}
void test_gpio_led() {
    for (bool active_high : {false, true}) {
        calls.clear();
        const int off = active_high ? 0 : 1, on = 1 - off;
        bth::EspGpioLed led(2, active_high);
        CHECK_LED(led.set(true) == bth::Status::invalid_state && calls.empty());
        CHECK_LED(led.init() == bth::Status::ok);
        CHECK_LED(calls == (std::vector<int>{off, 2}));
        CHECK_LED(led.init() == bth::Status::invalid_state && calls.size() == 2);
        CHECK_LED(led.set(true) == bth::Status::ok && calls.back() == on);
        CHECK_LED(led.set(false) == bth::Status::ok && calls.back() == off);
        fail_level = true;
        CHECK_LED(led.set(true) == bth::Status::hardware_error);
        fail_level = false;
        led.deinit();
        CHECK_LED(calls.back() == off);
        const auto count = calls.size();
        led.deinit();
        CHECK_LED(calls.size() == count && led.set(true) == bth::Status::invalid_state);
        CHECK_LED(led.init() == bth::Status::ok);
        led.deinit();
        for (bool fail_preload : {false, true}) {
            calls.clear();
            fail_level = fail_preload;
            fail_config = !fail_preload;
            CHECK_LED(led.init() == bth::Status::hardware_error);
            CHECK_LED(calls.size() == (fail_preload ? 1U : 2U));
            CHECK_LED(led.set(true) == bth::Status::invalid_state);
            fail_level = fail_config = false;
            led.deinit();
            CHECK_LED(calls.back() == off);
            CHECK_LED(led.init() == bth::Status::ok);
            led.deinit();
        }
    }
    calls.clear();
    bth::EspGpioLed invalid(-1, true);
    CHECK_LED(invalid.init() == bth::Status::invalid_argument);
    invalid.deinit();
    CHECK_LED(calls.empty());
    {
        bth::EspGpioLed led(2, false);
        CHECK_LED(led.init() == bth::Status::ok && led.set(true) == bth::Status::ok);
    }
    CHECK_LED(calls.back() == 1);
}
