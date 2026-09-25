#include "bth/heartbeat/heartbeat.hpp"

namespace bth {

Status Heartbeat::init() noexcept {
    if (interval_ms_ == 0) {
        return Status::invalid_argument;
    }
    initialized_ = true;
    return Status::ok;
}

Status Heartbeat::start() noexcept {
    if (!initialized_ || running_) {
        return Status::invalid_state;
    }
    first_report_ = true;
    running_ = true;
    return Status::ok;
}

Status Heartbeat::tick(std::uint64_t now_ms) noexcept {
    if (!running_) {
        return Status::invalid_state;
    }
    if (first_report_ || now_ms - last_report_ms_ >= interval_ms_) {
        sink_.report(now_ms);
        last_report_ms_ = now_ms;
        first_report_ = false;
    }
    return Status::ok;
}

void Heartbeat::stop() noexcept {
    running_ = false;
}

void Heartbeat::deinit() noexcept {
    stop();
    initialized_ = false;
}

}  // namespace bth
