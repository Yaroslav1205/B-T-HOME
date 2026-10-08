#include "bth/mains/mains.hpp"
#include <algorithm>
#include <cmath>

namespace bth {
void MainsModule::reset() noexcept {
    reading_ = {};
    sum_ = squares_ = 0;
    loss_ = false;
}
Status MainsModule::init() noexcept {
    if (acquired_) return Status::invalid_state;
    if (!std::isfinite(scale_) || scale_ < 0) return Status::invalid_argument;
    acquired_ = true;
    const auto status = input_.init();
    ready_ = status == Status::ok;
    return status;
}
Status MainsModule::start() noexcept {
    if (!ready_ || started_) return Status::invalid_state;
    reset();
    timed_ = false;
    history_ = {};
    started_ = true;  // stop must also clean up a partially failed start.
    return input_.start();
}
Status MainsModule::tick(std::uint64_t now_ms) noexcept {
    if (!started_) return Status::invalid_state;
    if (!timed_) { last_ms_ = now_ms; timed_ = true; }
    std::uint16_t samples[256];
    std::size_t count = 0;
    bool loss = false;
    const auto status = input_.read(samples, 256, count, loss);
    if (status != Status::ok) return status;
    if (count > 256) return Status::internal_error;
    loss_ = loss_ || loss;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned value = samples[i];
        sum_ += value;
        squares_ += static_cast<double>(value) * value;
        reading_.minimum = std::min(reading_.minimum, value);
        reading_.maximum = std::max(reading_.maximum, value);
        ++reading_.samples;
    }
    if (now_ms - last_ms_ >= 1000) {
        if (reading_.samples) {
            reading_.mean = sum_ / reading_.samples;
            reading_.rms = std::sqrt(std::max(0.0, squares_ / reading_.samples -
                                                   reading_.mean * reading_.mean));
        }
        // Require a useful window and reject clipping, gaps and long scheduling stalls.
        reading_.valid = !loss_ && reading_.samples >= 1800 && reading_.samples <= 2400 &&
                         now_ms - last_ms_ <= 1200 && reading_.minimum > 0 && reading_.maximum < 4095;
        reading_.calibrated = scale_ > 0;
        reading_.volts = reading_.rms * scale_;
        reading_.history = history_.update(reading_.volts, reading_.valid && reading_.calibrated, now_ms);
        sink_.report(reading_);
        last_ms_ = now_ms;
        reset();
    }
    return Status::ok;
}
void MainsModule::stop() noexcept {
    if (started_) input_.stop();
    started_ = timed_ = false;
    reset();
}
void MainsModule::deinit() noexcept {
    stop();
    if (acquired_) input_.deinit();
    acquired_ = ready_ = false;
}
}  // namespace bth
