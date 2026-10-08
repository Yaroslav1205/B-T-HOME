#pragma once
#include "bth/core/module.hpp"

namespace bth {
// Calibrated ADC-pin millivolts accumulated by a shared sampler since the last read.
struct DcBatch {
    double millivolt_sum = 0;
    unsigned samples = 0;
    bool invalid = false;
};
class DcInput {
public:
    virtual ~DcInput() = default;
    // Nonblocking snapshot and reset; the injected provider owns acquisition resources.
    virtual Status read_dc(DcBatch& batch) noexcept = 0;
};
struct DcReading { double volts = 0; unsigned samples = 0; bool valid = false; };
class DcSink {
public:
    virtual ~DcSink() = default;
    virtual void report(const DcReading& reading) noexcept = 0;
};
class DcModule final : public Module {
public:
    DcModule(DcInput& input, DcSink& sink, double divider_ratio,
             Dependencies providers = {}) noexcept
        : input_(input), sink_(sink), ratio_(divider_ratio), providers_(providers) {}
    const char* id() const noexcept override { return "dc"; }
    Dependencies dependencies() const noexcept override { return providers_; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    DcInput& input_;
    DcSink& sink_;
    const double ratio_;
    const Dependencies providers_;
    DcBatch window_{};
    std::uint64_t last_ms_ = 0;
    bool initialized_ = false, running_ = false, timed_ = false;
};
}  // namespace bth
