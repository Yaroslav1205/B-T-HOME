#pragma once
#include <cstdint>

namespace bth {
enum class ButtonAction { none, pair, toggle_mode };

// Poll from the owner task. Actions occur only after a debounced release.
class BootGesture {
public:
    ButtonAction update(bool pressed, std::uint64_t now) noexcept {
        if (!seen_ || now < last_ || now - last_ > 250) {
            seen_ = true;
            raw_ = stable_ = pressed;
            armed_ = !pressed;
            changed_ = last_ = now;
            return ButtonAction::none;
        }
        last_ = now;
        if (pressed != raw_) { raw_ = pressed; changed_ = now; }
        if (raw_ == stable_ || now - changed_ < 40) return ButtonAction::none;
        stable_ = raw_;
        if (stable_) { pressed_since_ = changed_; return ButtonAction::none; }
        if (!armed_) { armed_ = true; return ButtonAction::none; }
        const auto duration = changed_ - pressed_since_;
        if (duration >= 3000) return ButtonAction::toggle_mode;
        return duration >= 50 ? ButtonAction::pair : ButtonAction::none;
    }
private:
    bool seen_ = false, raw_ = false, stable_ = false, armed_ = false;
    std::uint64_t changed_ = 0, last_ = 0, pressed_since_ = 0;
};
}  // namespace bth
