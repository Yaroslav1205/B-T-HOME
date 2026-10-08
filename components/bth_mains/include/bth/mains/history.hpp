#pragma once
#include <cmath>
#include <cstdint>

namespace bth {
struct MainsPeriod {
    std::uint64_t duration_ms = 0;
    bool present = false;
    bool known = false;
};
struct MainsHistory { MainsPeriod current, previous; };

// A gap breaks continuity: never infer an outage from missing ADC data.
class MainsHistoryTracker {
public:
    MainsHistory update(double volts, bool valid, std::uint64_t now) noexcept {
        if (!valid || !std::isfinite(volts) || volts < 0 ||
            (history_.current.known && (now < last_ || now - last_ >= 2500))) {
            history_ = {};
        }
        if (!valid || !std::isfinite(volts) || volts < 0) return history_;
        const bool present = volts >= 100.0;
        if (!history_.current.known) {
            start_ = now;
            history_.current = {0, present, true};
        } else if (present != history_.current.present) {
            history_.previous = {now - start_, history_.current.present, true};
            history_.current = {0, present, true};
            start_ = now;
        }
        history_.current.duration_ms = now - start_;
        last_ = now;
        return history_;
    }
private:
    MainsHistory history_{};
    std::uint64_t start_ = 0, last_ = 0;
};
}  // namespace bth
