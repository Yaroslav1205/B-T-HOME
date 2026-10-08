#pragma once
#include "bth/core/module.hpp"
#include "bth/mains/history.hpp"

namespace bth {
struct MainsReading {
    MainsHistory history{};
    double rms = 0;
    double mean = 0;
    double volts = 0;
    unsigned samples = 0;
    unsigned minimum = 4095;
    unsigned maximum = 0;
    bool valid = false;
    bool calibrated = false;
};

// Samples are uniformly spaced ADC counts. A replacement ADC supplies its own scale.
class MainsInput {
public:
    virtual ~MainsInput() = default;
    virtual Status init() noexcept = 0;
    virtual Status start() noexcept = 0;
    // Nonblocking, bounded read. loss marks the entire reporting window invalid.
    virtual Status read(std::uint16_t* samples, std::size_t capacity,
                        std::size_t& count, bool& loss) noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void deinit() noexcept = 0;
};
class MainsSink {
public:
    virtual ~MainsSink() = default;
    virtual void report(const MainsReading& reading) noexcept = 0;
};
class MainsModule final : public Module {
public:
    MainsModule(MainsInput& input, MainsSink& sink, double volts_per_count = 0) noexcept
        : input_(input), sink_(sink), scale_(volts_per_count) {}
    const char* id() const noexcept override { return "mains"; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    void reset() noexcept;
    MainsInput& input_;
    MainsSink& sink_;
    double scale_;
    MainsReading reading_{};
    MainsHistoryTracker history_{};
    double sum_ = 0, squares_ = 0;
    std::uint64_t last_ms_ = 0;
    bool acquired_ = false, ready_ = false, started_ = false, timed_ = false, loss_ = false;
};
}  // namespace bth
