#pragma once
#include "bth/core/module.hpp"
#include "bth/mains/mains.hpp"
#include "bth/dc/dc.hpp"

namespace bth {
struct BatteryEstimate {
    std::uint64_t minutes = 0;
    bool valid = false;
    bool mains_known = false;
    bool mains_present = false;
};
class BatterySink {
public:
    virtual ~BatterySink() = default;
    virtual void report_battery(const BatteryEstimate& estimate) noexcept = 0;
};
// Synchronous storage is used only during lifecycle and owner-task service().
class BatteryStore {
public:
    virtual ~BatteryStore() = default;
    virtual Status init() noexcept = 0;
    virtual Status load(std::uint64_t& charge_ma_ms, bool& found) noexcept = 0;
    virtual Status save(std::uint64_t charge_ma_ms) noexcept = 0;
    virtual void deinit() noexcept = 0;
};
struct BatteryConfig {
    std::uint32_t capacity_mah = 12000;
    std::uint32_t discharge_ma = 100;
    std::uint32_t charge_ma = 1500;
    double cutoff_volts = 10.0;
};
class BatteryModule final : public Module, public MainsSink, public DcSink {
public:
    BatteryModule(BatteryStore& store, BatterySink& sink, BatteryConfig config,
                  Dependencies required = {}) noexcept
        : store_(store), sink_(sink), config_(config), required_(required) {}
    const char* id() const noexcept override { return "battery"; }
    Dependencies dependencies() const noexcept override { return required_; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now_ms) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    void report(const MainsReading& reading) noexcept override;
    void report(const DcReading& reading) noexcept override;
    // Call after Runtime::tick, never from a callback or ISR. May commit NVS.
    Status service() noexcept;
private:
    static constexpr std::uint64_t freshness_ms = 2500;
    static constexpr std::uint64_t checkpoint_ms = 300000;
    Status save() noexcept;
    BatteryStore& store_;
    BatterySink& sink_;
    const BatteryConfig config_;
    const Dependencies required_;
    std::uint64_t charge_ = 0, saved_charge_ = 0;
    std::uint64_t last_tick_ = 0, last_save_ = 0, mains_at_ = 0, dc_at_ = 0;
    bool acquired_ = false, initialized_ = false, running_ = false;
    bool timed_ = false, previous_valid_ = false, previous_mains_ = false;
    bool mains_fresh_ = false, dc_fresh_ = false, mains_received_ = false, dc_received_ = false;
    bool mains_valid_ = false, mains_present_ = false, dc_valid_ = false;
    bool save_due_ = false, save_failed_ = false;
    double dc_volts_ = 0;
};
} // namespace bth
