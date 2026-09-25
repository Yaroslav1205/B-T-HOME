#pragma once

#include "bth/core/module.hpp"

namespace bth {

// Implemented by the platform adapter, replaced with a fake in host tests.
class HeartbeatSink {
public:
    virtual ~HeartbeatSink() = default;
    virtual void report(std::uint64_t uptime_ms) noexcept = 0;
};

class Heartbeat final : public Module {
public:
    Heartbeat(HeartbeatSink& sink, std::uint32_t interval_ms) noexcept
        : sink_(sink), interval_ms_(interval_ms) {}

    const char* id() const noexcept override { return "heartbeat"; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;

private:
    HeartbeatSink& sink_;
    const std::uint32_t interval_ms_;
    std::uint64_t last_report_ms_ = 0;
    bool initialized_ = false;
    bool running_ = false;
    bool first_report_ = true;
};

}  // namespace bth
