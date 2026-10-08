#pragma once

namespace bth {
enum class PairingAction { none, rollback, open, factory_reset };

// Keep a physical pairing request across asynchronous SDK rollback.
class PairingRetry {
public:
    void request(bool reset_matter = false) noexcept {
        if (reset_dispatched_) return;
        requested_ = true;
        reset_matter_ = reset_matter_ || reset_matter;
    }
    bool pending() const noexcept { return requested_; }
    PairingAction advance(bool fully_disarmed) noexcept {
        if (!requested_) return PairingAction::none;
        // The SDK reset owns fail-safe cleanup and reboot. Dispatch exactly once,
        // even if an unfinished commissioning transaction cannot roll back.
        if (reset_matter_) {
            requested_ = cleanup_requested_ = reset_matter_ = false;
            reset_dispatched_ = true;
            return PairingAction::factory_reset;
        }
        if (fully_disarmed) {
            requested_ = cleanup_requested_ = false;
            return PairingAction::open;
        }
        if (cleanup_requested_) return PairingAction::none;
        cleanup_requested_ = true;
        return PairingAction::rollback;
    }
private:
    bool requested_ = false;
    bool cleanup_requested_ = false;
    bool reset_matter_ = false;
    bool reset_dispatched_ = false;
};
}  // namespace bth
