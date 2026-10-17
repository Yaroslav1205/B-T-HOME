#include "bth/radio/radio_mode.hpp"

namespace bth {
Status RadioMode::init() noexcept {
    if (initialized_ || started_) return Status::invalid_state;
    if (!loaded_) {
        const auto result = store_.load_mode(mode_);
        if (result != Status::ok) return result;
        if (mode_ != RadioSelection::ap && mode_ != RadioSelection::station && mode_ != RadioSelection::zigbee)
            return Status::invalid_argument;
        loaded_ = true;
    }
    control_.select_station(mode_ == RadioSelection::station);
    const unsigned count = configuration_mode() ? 2 : 1;
    while (initialized_ < count) {
        const auto result = child(initialized_++).init();
        if (result != Status::ok) { deinit(); return result; }
    }
    return Status::ok;
}
Status RadioMode::start() noexcept {
    if (!initialized_ || started_) return Status::invalid_state;
    while (started_ < initialized_) {
        const auto result = child(started_++).start();
        if (result != Status::ok) { deinit(); return result; }
    }
    return Status::ok;
}
Status RadioMode::tick(std::uint64_t now) noexcept {
    if (!started_ || started_ != initialized_) return Status::invalid_state;
    for (unsigned i = 0; i < started_; ++i) {
        const auto result = child(i).tick(now);
        if (result != Status::ok) return result;
    }
    return Status::ok;
}
void RadioMode::stop() noexcept {
    while (started_) child(--started_).stop();
}
void RadioMode::deinit() noexcept {
    stop();
    while (initialized_) child(--initialized_).deinit();
}
Status RadioMode::toggle() noexcept {
    if (!started_) return Status::invalid_state;
    deinit();
    const auto next = mode_ == RadioSelection::ap ? RadioSelection::station :
        mode_ == RadioSelection::station ? RadioSelection::zigbee : RadioSelection::ap;
    const auto saved = store_.save_mode(next);
    if (saved != Status::ok) return saved;
    mode_ = next;
    const auto result = init();
    return result == Status::ok ? start() : result;
}
}  // namespace bth
