#include "bth/platform/esp_rgb_led.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

namespace bth {
namespace {
Status checked(esp_err_t result) noexcept {
    if (result == ESP_OK) { return Status::ok; }
    ESP_LOGE("rgb_led", "%s", esp_err_to_name(result));
    return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
void encode(std::array<rmt_symbol_word_t, 25>& frame, bool on) noexcept {
    // WS2812 GRB, dim white (16/255 in each channel), 10 MHz RMT clock.
    for (unsigned i = 0; i < 24; ++i) {
        const bool bit = on && ((0x101010U >> (23 - i)) & 1U);
        frame[i].level0 = 1;
        frame[i].duration0 = bit ? 9 : 3;
        frame[i].level1 = 0;
        frame[i].duration1 = bit ? 3 : 9;
    }
    // 300 us low latch accommodates WS2812 variants with a longer reset time.
    frame[24].level0 = 0;
    frame[24].duration0 = 1500;
    frame[24].level1 = 0;
    frame[24].duration1 = 1500;
}
}
Status EspRgbLed::init() noexcept {
    if (channel_ || encoder_) { return Status::invalid_state; }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_)) { return Status::invalid_argument; }
    encode(on_frame_, true);
    encode(off_frame_, false);
    rmt_tx_channel_config_t config{};
    config.gpio_num = static_cast<gpio_num_t>(gpio_);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 10000000;
    config.mem_block_symbols = 64;
    config.trans_queue_depth = 2;
    auto result = rmt_new_tx_channel(&config, &channel_);
    if (result != ESP_OK) { return checked(result); }
    rmt_copy_encoder_config_t encoder_config{};
    result = rmt_new_copy_encoder(&encoder_config, &encoder_);
    if (result != ESP_OK) { return checked(result); }
    result = rmt_enable(channel_);
    if (result != ESP_OK) { return checked(result); }
    enabled_ = true;
    const auto status = set(false);
    if (status != Status::ok) { return status; }
    return checked(rmt_tx_wait_all_done(channel_, 100));
}
Status EspRgbLed::set(bool on) noexcept {
    if (!enabled_) { return Status::invalid_state; }
    rmt_transmit_config_t config{};
    config.flags.queue_nonblocking = 1;
    const auto& frame = on ? on_frame_ : off_frame_;
    return checked(rmt_transmit(channel_, encoder_, frame.data(), sizeof(frame), &config));
}
void EspRgbLed::deinit() noexcept {
    if (enabled_) {
        (void)checked(rmt_tx_wait_all_done(channel_, 100));
        (void)set(false);
        (void)checked(rmt_tx_wait_all_done(channel_, 100));
        // Disable terminates transmission before the encoder or payload can be destroyed.
        (void)checked(rmt_disable(channel_));
        enabled_ = false;
    }
    if (channel_) {
        (void)checked(rmt_del_channel(channel_));
        channel_ = nullptr;
    }
    if (encoder_) {
        (void)checked(rmt_del_encoder(encoder_));
        encoder_ = nullptr;
    }
}
}  // namespace bth
