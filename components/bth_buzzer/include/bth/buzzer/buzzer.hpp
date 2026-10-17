#pragma once

#include "bth/core/module.hpp"
#include "bth/mains/mains.hpp"
#include "bth/buzzer/settings.hpp"

namespace bth {

class BuzzerOutput {
public:
    virtual ~BuzzerOutput() = default;
    virtual Status init() noexcept = 0;
    virtual Status set(bool on) noexcept = 0;
    virtual void deinit() noexcept = 0;
};

class BuzzerModule final : public Module, public MainsSink {
public:
    explicit BuzzerModule(BuzzerOutput& output) noexcept : output_(output) {}
    const char* id() const noexcept override { return "buzzer"; }
    Dependencies dependencies() const noexcept override { return {providers_, 1}; }
    void report(const MainsReading& reading) noexcept override;
    // Runtime owner only. Zero disables all outage sounds at the next tick.
    Status set_interval_seconds(std::uint32_t seconds) noexcept;
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;

private:
    inline static constexpr const char* providers_[] = {"mains"};
    BuzzerOutput& output_;
    std::uint64_t report_ms_ = 0, pulse_ms_ = 0;
    std::uint32_t interval_seconds_ = buzzer_default_interval_seconds;
    bool acquired_ = false, initialized_ = false;
    bool running_ = false;
    bool pending_ = false, absent_ = false, fresh_ = false;
    bool outage_ = false, on_ = false;
};

}  // namespace bth
