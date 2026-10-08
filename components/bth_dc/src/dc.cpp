#include "bth/dc/dc.hpp"
#include <cmath>

namespace bth {
Status DcModule::init() noexcept {
    if (initialized_) return Status::invalid_state;
    if (!std::isfinite(ratio_) || ratio_ < 1) return Status::invalid_argument;
    initialized_ = true;
    return Status::ok;
}
Status DcModule::start() noexcept {
    if (!initialized_ || running_) return Status::invalid_state;
    DcBatch discarded;
    const auto status = input_.read_dc(discarded);
    if (status != Status::ok) return status;
    window_ = {};
    timed_ = false;
    running_ = true;
    return Status::ok;
}
Status DcModule::tick(std::uint64_t now_ms) noexcept {
    if (!running_) return Status::invalid_state;
    if (!timed_) { last_ms_ = now_ms; timed_ = true; }
    DcBatch batch;
    const auto status = input_.read_dc(batch);
    if (status != Status::ok) return status;
    // Bound accumulation even if a provider misbehaves or scheduling stalls.
    if (batch.samples > 4096 || window_.samples > 4096 - batch.samples ||
        !std::isfinite(batch.millivolt_sum) || batch.millivolt_sum < 0) {
        window_.invalid = true;
    } else {
        window_.samples += batch.samples;
        window_.millivolt_sum += batch.millivolt_sum;
    }
    window_.invalid = window_.invalid || batch.invalid;
    if (now_ms - last_ms_ >= 1000) {
        DcReading reading;
        reading.samples = window_.samples;
        if (window_.samples) reading.volts = window_.millivolt_sum / window_.samples * ratio_ / 1000;
        reading.valid = !window_.invalid && window_.samples >= 1800 && window_.samples <= 2400 &&
                        now_ms - last_ms_ <= 1200 && std::isfinite(reading.volts) &&
                        reading.volts >= 0 && reading.volts < 30.05;
        sink_.report(reading);
        window_ = {};
        last_ms_ = now_ms;
    }
    return Status::ok;
}
void DcModule::stop() noexcept { running_ = timed_ = false; window_ = {}; }
void DcModule::deinit() noexcept { stop(); initialized_ = false; }
}  // namespace bth
