#pragma once
#include <cstdint>
namespace bth {
// One bounded attempt per slot, including association and DHCP. Owner task only.
class StationRetry {
public:
    void reset(bool primary, bool backup) noexcept {
        enabled_[0] = primary; enabled_[1] = backup;
        next_ = 0; timed_ = connected_ = false;
    }
    int poll(std::uint64_t now, bool connected) noexcept {
        if (connected) { connected_ = true; return -1; }
        if (connected_) { connected_ = false; timed_ = false; }
        if (timed_ && now >= since_ && now - since_ < 15000) return -1;
        for (unsigned i = 0; i < 2; ++i) {
            const auto slot = next_;
            next_ = (next_ + 1) % 2;
            if (!enabled_[slot]) continue;
            since_ = now; timed_ = true;
            return static_cast<int>(slot);
        }
        return -1;
    }
private:
    bool enabled_[2]{};
    bool timed_ = false, connected_ = false;
    unsigned next_ = 0;
    std::uint64_t since_ = 0;
};
} // namespace bth
