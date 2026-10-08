#pragma once
#include <cstdint>

namespace bth {
enum class ZigbeeState { disabled, starting, idle, joining, joined, rejoining, resetting, error };
enum class ZigbeeCommand { join, reset };
struct ZigbeeSnapshot {
    ZigbeeState state = ZigbeeState::disabled;
    unsigned channel = 0;
    unsigned seconds_left = 0;
};
class ZigbeeControl {
public:
    virtual ~ZigbeeControl() = default;
    virtual ZigbeeSnapshot snapshot() const noexcept = 0;
    virtual bool request(ZigbeeCommand command) noexcept = 0;
};
inline const char* zigbee_state_name(ZigbeeState state) noexcept {
    switch (state) {
    case ZigbeeState::starting: return "starting";
    case ZigbeeState::idle: return "idle";
    case ZigbeeState::joining: return "joining";
    case ZigbeeState::joined: return "joined";
    case ZigbeeState::rejoining: return "rejoining";
    case ZigbeeState::resetting: return "resetting";
    case ZigbeeState::error: return "error";
    default: return "disabled";
    }
}

// Single-owner policy. Scans never overlap; retries stop after a bounded window.
class ZigbeeJoinWindow {
public:
    void open(std::uint64_t now) noexcept { until_ = now + 120000; next_ = now; }
    void close() noexcept { until_ = 0; }
    bool active(std::uint64_t now) const noexcept { return until_ != 0 && now < until_; }
    unsigned remaining(std::uint64_t now) const noexcept {
        return active(now) ? static_cast<unsigned>((until_ - now + 999) / 1000) : 0;
    }
    bool due(std::uint64_t now) const noexcept { return active(now) && !in_flight_ && now >= next_; }
    void started() noexcept { in_flight_ = true; }
    void finished(std::uint64_t now, bool success) noexcept {
        in_flight_ = false; next_ = now + 5000;
        if (success) close();
    }
    bool needs_cancel(std::uint64_t now) const noexcept { return in_flight_ && !active(now); }
private:
    std::uint64_t until_ = 0, next_ = 0;
    bool in_flight_ = false;
};
}  // namespace bth
