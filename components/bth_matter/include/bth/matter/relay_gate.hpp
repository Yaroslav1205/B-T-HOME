#pragma once
#include <atomic>

namespace bth {
enum class MatterWrite { ignore, apply, reject };
// Initial SDK restoration must not overwrite the persisted RelayService state.
// Closing the gate precedes stopping SDK tasks and releasing relay resources.
class MatterRelayGate {
public:
    void start() noexcept { state_ = State::running; }
    void stop() noexcept { state_ = State::stopped; }
    MatterWrite classify(bool local_update) const noexcept {
        const auto state = state_.load();
        if (state == State::stopped) return MatterWrite::reject;
        if (local_update || state == State::restoring) return MatterWrite::ignore;
        return MatterWrite::apply;
    }
private:
    enum class State { restoring, running, stopped };
    std::atomic<State> state_{State::restoring};
};
}  // namespace bth
