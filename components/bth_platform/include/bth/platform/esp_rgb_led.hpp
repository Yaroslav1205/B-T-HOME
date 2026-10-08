#pragma once
#include <array>
#include "bth/startup_blink/startup_blink.hpp"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

namespace bth {
class EspRgbLed final : public LedOutput {
public:
    explicit EspRgbLed(int gpio) noexcept : gpio_(gpio) {}
    ~EspRgbLed() override { deinit(); }
    EspRgbLed(const EspRgbLed&) = delete;
    EspRgbLed& operator=(const EspRgbLed&) = delete;
    Status init() noexcept override;
    Status set(bool on) noexcept override;
    void deinit() noexcept override;
private:
    int gpio_;
    rmt_channel_handle_t channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    bool enabled_ = false;
    // Persistent payloads: RMT uses them asynchronously until transmission ends.
    std::array<rmt_symbol_word_t, 25> on_frame_{};
    std::array<rmt_symbol_word_t, 25> off_frame_{};
};
}  // namespace bth
