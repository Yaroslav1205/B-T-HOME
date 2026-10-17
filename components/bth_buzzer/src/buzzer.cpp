#include "bth/buzzer/buzzer.hpp"
#include <cmath>

namespace bth {

Status BuzzerModule::init() noexcept {
    if (acquired_) return Status::invalid_state;
    acquired_ = true;
    const auto result = output_.init();
    initialized_ = result == Status::ok;
    return result;
}

Status BuzzerModule::start() noexcept {
    if (!initialized_ || running_) {
        return Status::invalid_state;
    }
    pending_ = absent_ = fresh_ = outage_ = on_ = false;
    const auto result = output_.set(false);
    running_ = result == Status::ok;
    return result;
}

void BuzzerModule::report(const MainsReading& reading) noexcept {
    if (!running_) return;
    // Use the same confirmed outage state as OLED and battery estimation.
    absent_ = reading.valid && reading.calibrated && std::isfinite(reading.volts) &&
              reading.volts >= 0 && reading.history.current.known &&
              !reading.history.current.present;
    pending_ = true;
}

Status BuzzerModule::set_interval_seconds(std::uint32_t seconds) noexcept {
    if (seconds > buzzer_max_interval_seconds) return Status::invalid_argument;
    interval_seconds_ = seconds;
    return Status::ok;
}

Status BuzzerModule::tick(std::uint64_t now_ms) noexcept {
    if (!running_) return Status::invalid_state;
    if (pending_) {
        report_ms_ = now_ms;
        pending_ = false;
        fresh_ = true;
    }
    if (interval_seconds_ == 0 || !fresh_ || now_ms < report_ms_ ||
        now_ms - report_ms_ >= 2500 || !absent_) {
        outage_ = false;
        if (!on_) return Status::ok;
        const auto result = output_.set(false);
        if (result == Status::ok) on_ = false;
        return result;
    }
    if (on_ && now_ms - pulse_ms_ >= 200) {
        const auto result = output_.set(false);
        if (result != Status::ok) return result;
        on_ = false;
    }
    // Repeat from the actual pulse start; never catch up with a burst of beeps.
    if (!outage_ || now_ms - pulse_ms_ >= static_cast<std::uint64_t>(interval_seconds_) * 1000) {
        const auto result = output_.set(true);
        if (result != Status::ok) return result;
        on_ = outage_ = true;
        pulse_ms_ = now_ms;
    }
    return Status::ok;
}

void BuzzerModule::stop() noexcept {
    if (initialized_) (void)output_.set(false);
    running_ = false;
    pending_ = absent_ = fresh_ = outage_ = on_ = false;
}

void BuzzerModule::deinit() noexcept {
    stop();
    if (acquired_) output_.deinit();
    acquired_ = initialized_ = false;
}

}  // namespace bth
