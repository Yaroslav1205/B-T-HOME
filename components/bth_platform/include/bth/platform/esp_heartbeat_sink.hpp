#pragma once

#include "bth/heartbeat/heartbeat.hpp"

namespace bth {

class EspHeartbeatSink final : public HeartbeatSink {
public:
    void report(std::uint64_t uptime_ms) noexcept override;
};

}  // namespace bth
