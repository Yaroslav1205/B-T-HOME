#pragma once
#include "bth/startup_blink/startup_blink.hpp"

namespace bth {
class EspGpioLed final : public LedOutput {
public:
    EspGpioLed(int gpio, bool active_high) noexcept : gpio_(gpio), active_high_(active_high) {}
    ~EspGpioLed() override { deinit(); }
    EspGpioLed(const EspGpioLed&) = delete;
    EspGpioLed& operator=(const EspGpioLed&) = delete;
    Status init() noexcept override;
    Status set(bool on) noexcept override;
    void deinit() noexcept override;
private:
    int gpio_;
    bool active_high_;
    bool acquired_ = false;
    bool ready_ = false;
};
}  // namespace bth
