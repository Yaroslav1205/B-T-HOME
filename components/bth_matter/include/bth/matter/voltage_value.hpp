#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace bth {
// Conservative signed Matter measurement range; reject values before casting.
inline constexpr std::int64_t voltage_max_millivolts = (INT64_C(1) << 62) - 1;
inline bool usable_voltage(double volts, bool valid) noexcept {
    return valid && std::isfinite(volts) && volts >= 0.0 &&
        volts * 1000.0 < static_cast<double>(voltage_max_millivolts);
}
struct MatterVoltageValue {
    std::int64_t millivolts;
    bool valid;
};
inline MatterVoltageValue matter_voltage_value(double volts, bool valid) noexcept {
    if (!usable_voltage(volts, valid)) return {0, false};
    return {static_cast<std::int64_t>(volts * 1000.0 + 0.5), true};
}
struct MatterTemperatureValue {
    std::int16_t centi_degrees;
    bool valid;
};
inline MatterTemperatureValue compatibility_temperature_value(double volts, bool valid) noexcept {
    if (!valid || !std::isfinite(volts) || volts < 0.0 || volts > 300.0) return {0, false};
    return {static_cast<std::int16_t>(volts * 100.0 + 0.5), true};
}

class RollingMainsVoltageAverage {
public:
    MatterTemperatureValue add(double volts, bool valid) noexcept {
        if (!compatibility_temperature_value(volts, valid).valid) {
            reset();
            return {0, false};
        }
        if (count_ == samples_.size()) {
            sum_ -= samples_[next_];
        } else {
            ++count_;
        }
        samples_[next_] = volts;
        sum_ += volts;
        next_ = (next_ + 1) % samples_.size();
        return {static_cast<std::int16_t>(sum_ / count_ * 100.0 + 0.5), true};
    }

private:
    void reset() noexcept {
        samples_ = {};
        sum_ = 0;
        count_ = next_ = 0;
    }

    std::array<double, 60> samples_{};
    double sum_ = 0;
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};
} // namespace bth
