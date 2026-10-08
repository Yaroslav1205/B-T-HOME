#pragma once
#include <cmath>
#include <cstdint>
#include "bth/mains/history.hpp"

namespace bth {
struct WebVoltage {
    double volts = 0;
    std::uint64_t updated_ms = 0;
    bool received = false;
    bool valid = false;
    bool calibrated = true;
};
struct WebVoltages { WebVoltage mains, battery; MainsHistory history{}; };

inline const char* voltage_status(const WebVoltage& value, std::uint64_t now_ms) noexcept {
    if (!value.received) return "waiting";
    if (now_ms < value.updated_ms || now_ms - value.updated_ms >= 2500) return "stale";
    if (!value.calibrated) return "uncalibrated";
    if (!value.valid || !std::isfinite(value.volts) || value.volts < 0 || value.volts >= 999.95)
        return "invalid";
    return "ok";
}
}  // namespace bth
