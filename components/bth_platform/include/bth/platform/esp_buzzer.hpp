#pragma once
#include "bth/buzzer/buzzer.hpp"

namespace bth {
class EspBuzzer final : public BuzzerOutput {
public:
    EspBuzzer(int gpio, bool active_high) noexcept : gpio_(gpio), active_high_(active_high) {}
    ~EspBuzzer() override { deinit(); }
    EspBuzzer(const EspBuzzer&) = delete;
    EspBuzzer& operator=(const EspBuzzer&) = delete;
    Status init() noexcept override;
    Status set(bool on) noexcept override;
    void deinit() noexcept override;
private:
    int gpio_;
    bool active_high_;
    bool acquired_ = false, ready_ = false;
};
}  // namespace bth
