#pragma once
#include "bth/core/module.hpp"
#include "bth/radio/button.hpp"

namespace bth {
class BootButton final : public Module {
public:
    BootButton(int gpio, bool active_low) noexcept : gpio_(gpio), active_low_(active_low) {}
    const char* id() const noexcept override { return "boot_button"; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void stop() noexcept override { running_ = false; action_ = ButtonAction::none; }
    void deinit() noexcept override;
    ButtonAction take_action() noexcept {
        const auto result = action_; action_ = ButtonAction::none; return result;
    }
private:
    const int gpio_;
    const bool active_low_;
    bool acquired_ = false, running_ = false;
    BootGesture gesture_{};
    ButtonAction action_ = ButtonAction::none;
};
}  // namespace bth
