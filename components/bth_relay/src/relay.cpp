#include "bth/relay/relay.hpp"

namespace bth {
Status RelayController::init() noexcept {
    if (store_acquired_ || output_acquired_) { return Status::invalid_state; }
    state_ = {};
    store_acquired_ = true;
    auto result = store_.init();
    if (result == Status::ok) { result = store_.load(state_.saved_on); }
    if (result != Status::ok) { state_.error = result; return result; }
    state_.storage_known = true;
    output_acquired_ = true;
    result = output_.init();
    if (result != Status::ok) { state_.error = result; return result; }
    state_.output_known = true;
    initialized_ = true;
    return Status::ok;
}
Status RelayController::start() noexcept {
    if (!initialized_ || state_.ready || state_.error != Status::ok) {
        return Status::invalid_state;
    }
    const auto result = output_.set(state_.saved_on);
    state_.error = result;
    state_.output_known = result == Status::ok;
    if (result == Status::ok) {
        state_.on = state_.saved_on;
        state_.ready = true;
    }
    return result;
}
Status RelayController::set(bool on) noexcept {
    if (!state_.ready) { return Status::invalid_state; }
    if (on == state_.on && on == state_.saved_on) { return Status::ok; }
    // Persist first: a power cut after commit restores the requested state.
    // A failed commit has uncertain durability; refuse further writes until restart.
    auto result = store_.save(on);
    if (result != Status::ok) {
        state_.ready = false;
        state_.storage_known = false;
        state_.error = result;
        return result;
    }
    state_.saved_on = on;
    result = output_.set(on);
    state_.error = result;
    state_.output_known = result == Status::ok;
    if (result == Status::ok) { state_.on = on; }
    else { state_.ready = false; }
    return result;
}
void RelayController::stop() noexcept {
    if (initialized_) {
        const auto result = output_.set(false);
        state_.output_known = result == Status::ok;
        if (result == Status::ok) { state_.on = false; }
        else { state_.error = result; }
    }
    state_.ready = false;
}
void RelayController::deinit() noexcept {
    stop();
    if (output_acquired_) { output_.deinit(); }
    if (store_acquired_) { store_.deinit(); }
    output_acquired_ = false;
    store_acquired_ = false;
    initialized_ = false;
}
}  // namespace bth
