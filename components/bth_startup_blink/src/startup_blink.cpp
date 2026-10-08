#include "bth/startup_blink/startup_blink.hpp"

namespace bth {

Status StartupBlinkModule::init() noexcept {
    if (acquired_ || initialized_) { return Status::invalid_state; }
    if (config_.on_ms == 0 || config_.off_ms == 0 ||
        (config_.startup_ms != 0 && config_.startup_half_period_ms == 0)) { return Status::invalid_argument; }
    acquired_ = true;
    const auto result = output_.init();
    initialized_ = result == Status::ok;
    return result;
}

Status StartupBlinkModule::start() noexcept {
    if (!initialized_ || running_) { return Status::invalid_state; }
    completed_ = 0;
    phase_ = Phase::startup;
    first_tick_ = true;
    pattern_changed_ = false;
    running_ = true;
    return Status::ok;
}

Status StartupBlinkModule::set_output(bool on) noexcept {
    if (on_ == on) { return Status::ok; }
    const auto result = output_.set(on);
    if (result == Status::ok) { on_ = on; }
    return result;
}

Status StartupBlinkModule::tick(std::uint64_t now_ms) noexcept {
    if (!running_) { return Status::invalid_state; }
    if (phase_ == Phase::startup) pattern_changed_ = false;
    if (pattern_changed_) {
        pattern_changed_ = false;
        completed_ = 0;
        phase_ = count_ == 0 ? Phase::done : Phase::pause;
        changed_ms_ = now_ms;
        return set_output(!config_.pulse_on);
    }
    if (first_tick_) {
        first_tick_ = false;
        startup_started_ms_ = changed_ms_ = now_ms;
        if (config_.startup_ms != 0) { return set_output(true); }
        phase_ = count_ == 0 ? Phase::done : Phase::pulse;
        return set_output(count_ == 0 ? !config_.pulse_on : config_.pulse_on);
    }
    switch (phase_) {
    case Phase::startup:
        // End by elapsed time even when ticks were delayed; never replay missed edges.
        if (now_ms - startup_started_ms_ >= config_.startup_ms) {
            phase_ = count_ == 0 ? Phase::done : Phase::pause;
            changed_ms_ = now_ms;
            return set_output(!config_.pulse_on);
        }
        if (now_ms - changed_ms_ >= config_.startup_half_period_ms) {
            changed_ms_ = now_ms;
            return set_output(!on_);
        }
        break;
    case Phase::pause:
        if (now_ms - changed_ms_ < config_.repeat_pause_ms) { break; }
        completed_ = 0;
        phase_ = Phase::pulse;
        changed_ms_ = now_ms;
        return set_output(config_.pulse_on);
    case Phase::pulse:
        if (now_ms - changed_ms_ < config_.on_ms) { break; }
        ++completed_;
        phase_ = completed_ == count_
            ? (config_.repeat_pause_ms == 0 ? Phase::done : Phase::pause)
            : Phase::between;
        changed_ms_ = now_ms;
        return set_output(!config_.pulse_on);
    case Phase::between:
        if (now_ms - changed_ms_ < config_.off_ms) { break; }
        phase_ = Phase::pulse;
        changed_ms_ = now_ms;
        return set_output(config_.pulse_on);
    case Phase::done:
        break;
    }
    return Status::ok;
}

void StartupBlinkModule::stop() noexcept {
    // Retry off even after a failed set: the physical output state may be uncertain.
    if (initialized_ && running_) { (void)output_.set(false); }
    running_ = false;
    on_ = false;
}

void StartupBlinkModule::deinit() noexcept {
    stop();
    if (acquired_) { output_.deinit(); }
    acquired_ = false;
    initialized_ = false;
}

}  // namespace bth
