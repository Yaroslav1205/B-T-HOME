#pragma once

#include "bth/core/module.hpp"

namespace bth {

class LedOutput {
public:
    virtual ~LedOutput() = default;
    // Acquire the output in its off state. deinit must handle partial failure.
    virtual Status init() noexcept = 0;
    virtual Status set(bool on) noexcept = 0;
    // Leave the LED off and release resources; safe to repeat.
    virtual void deinit() noexcept = 0;
};

struct StartupBlinkConfig {
    std::uint32_t count = 5;
    std::uint32_t on_ms = 200;
    std::uint32_t off_ms = 200;
    // Logical light state during a digit pulse, independent of GPIO polarity.
    bool pulse_on = true;
    std::uint32_t startup_ms = 2000;
    std::uint32_t startup_half_period_ms = 50;
    // Also separates the startup burst from the first digit. 0 means one-shot.
    std::uint32_t repeat_pause_ms = 2000;
};

class StartupBlinkModule final : public Module {
public:
    explicit StartupBlinkModule(LedOutput& output, StartupBlinkConfig config = {}) noexcept
        : output_(output), config_(config), count_(config.count) {}
    const char* id() const noexcept override { return "startup_blink"; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    // Owner task only. A changed count starts a fresh group after the idle pause.
    // Updating the count during the startup burst does not restart the burst.
    void set_count(std::uint32_t count) noexcept {
        if (count_ != count) { count_ = count; pattern_changed_ = true; }
    }

private:
    enum class Phase { startup, pause, pulse, between, done };
    Status set_output(bool on) noexcept;
    LedOutput& output_;
    const StartupBlinkConfig config_;
    std::uint32_t count_;
    bool pattern_changed_ = false;
    std::uint64_t changed_ms_ = 0;
    std::uint64_t startup_started_ms_ = 0;
    Phase phase_ = Phase::startup;
    std::uint32_t completed_ = 0;
    bool acquired_ = false;
    bool initialized_ = false;
    bool running_ = false;
    bool first_tick_ = true;
    bool on_ = false;
};

}  // namespace bth
