#include "bth/battery/battery.hpp"
#include "bth/battery/settings.hpp"
#include <algorithm>
#include <cmath>

namespace bth {
Status BatteryModule::init() noexcept {
    if (acquired_) return Status::invalid_state;
    if (!config_.capacity_mah || !config_.discharge_ua || !config_.charge_ma ||
        !std::isfinite(config_.cutoff_volts) || config_.cutoff_volts <= 0)
        return Status::invalid_argument;
    acquired_ = true;
    auto result = store_.init();
    if (result != Status::ok) return result;
    bool found = false;
    result = store_.load(charge_, found);
    if (result != Status::ok) return result;
    const auto capacity = std::uint64_t{config_.capacity_mah} * 3600000;
    if (found && charge_ > capacity) return Status::invalid_argument;
    if (!found) {
        // User-confirmed full battery is the one-time initialization only.
        charge_ = capacity;
        result = store_.save(charge_);
        if (result != Status::ok) return result;
    }
    charge_ *= 1000; // Loaded/saved NVS units are mA*ms; runtime units are uA*ms.
    saved_charge_ = charge_;
    save_failed_ = save_due_ = false;
    initialized_ = true;
    return Status::ok;
}
Status BatteryModule::start() noexcept {
    if (!initialized_ || running_) return Status::invalid_state;
    timed_ = previous_valid_ = false;
    mains_fresh_ = dc_fresh_ = mains_received_ = dc_received_ = false;
    mains_valid_ = dc_valid_ = false;
    running_ = true;
    sink_.report_battery({});
    return Status::ok;
}
void BatteryModule::report(const MainsReading& reading) noexcept {
    mains_valid_ = reading.valid && reading.calibrated && reading.history.current.known;
    mains_present_ = reading.history.current.present;
    mains_fresh_ = true;
}
void BatteryModule::report(const DcReading& reading) noexcept {
    dc_volts_ = reading.volts;
    dc_valid_ = reading.valid && std::isfinite(reading.volts) && reading.volts >= 0 && reading.volts < 30.05;
    dc_fresh_ = true;
}
Status BatteryModule::tick(std::uint64_t now) noexcept {
    if (!running_) return Status::invalid_state;
    if (mains_fresh_) { mains_at_ = now; mains_received_ = true; mains_fresh_ = false; }
    if (dc_fresh_) { dc_at_ = now; dc_received_ = true; dc_fresh_ = false; }
    const bool mains_known = mains_received_ && mains_valid_ && now >= mains_at_ && now - mains_at_ < freshness_ms;
    const bool dc_known = dc_received_ && dc_valid_ && now >= dc_at_ && now - dc_at_ < freshness_ms;
    const bool valid = mains_known && dc_known;
    const auto capacity = std::uint64_t{config_.capacity_mah} * 3600000000ULL;
    // Integrate the preceding known interval. Never invent charge during gaps.
    if (timed_ && previous_valid_ && valid && now >= last_tick_ && now - last_tick_ < freshness_ms) {
        const auto elapsed = now - last_tick_;
        if (previous_mains_) {
            charge_ += std::min(capacity - charge_, elapsed * config_.charge_ma * 1000ULL);
        } else {
            charge_ -= std::min(charge_, elapsed * config_.discharge_ua);
        }
    }
    if (!timed_ || now < last_tick_) last_save_ = now;
    if (valid && !mains_present_ && dc_volts_ <= config_.cutoff_volts) {
        charge_ = 0;
        // Checkpoint depletion promptly, before the supply may disconnect.
        if (saved_charge_ != 0) save_due_ = true;
    }
    if (now - last_save_ >= checkpoint_ms && charge_ != saved_charge_) save_due_ = true;
    last_tick_ = now;
    timed_ = true;
    previous_valid_ = valid;
    previous_mains_ = mains_present_;
    sink_.report_battery({charge_ / (std::uint64_t{config_.discharge_ua} * 60000),
                          valid, mains_known, mains_known && mains_present_});
    return Status::ok;
}
Status BatteryModule::save() noexcept {
    const auto result = store_.save(charge_ / 1000);
    save_failed_ = result != Status::ok;
    if (result == Status::ok) {
        saved_charge_ = charge_;
        last_save_ = last_tick_;
        save_due_ = false;
    }
    return result;
}
Status BatteryModule::set_discharge_ma(std::uint32_t milliamps) noexcept {
    if (!milliamps || milliamps > battery_max_discharge_ma) return Status::invalid_argument;
    config_.discharge_ua = milliamps * 1000;
    return Status::ok;
}
Status BatteryModule::service() noexcept {
    if (!running_ || save_failed_) return Status::invalid_state;
    return save_due_ ? save() : Status::ok;
}
void BatteryModule::stop() noexcept {
    if (running_ && !save_failed_ && charge_ != saved_charge_) (void)save();
    running_ = false;
    sink_.report_battery({});
}
void BatteryModule::deinit() noexcept {
    stop();
    if (acquired_) store_.deinit();
    acquired_ = initialized_ = false;
}
} // namespace bth
