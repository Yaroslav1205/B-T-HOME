#pragma once

#include "bth/core/module.hpp"
#include "bth/battery/battery.hpp"
#include "bth/mains/mains.hpp"
#include <array>

namespace bth {

using OledFrame = std::array<std::uint8_t, 128 * 32 / 8>;

// Synchronous probing/transfers run only during setup/cleanup, never from tick.
class OledTransport {
public:
    virtual ~OledTransport() = default;
    virtual Status init() noexcept = 0;
    // Missing/unresponsive optional display: ok with present=false.
    virtual Status probe(bool& present) noexcept = 0;
    virtual Status write(const std::uint8_t* bytes, std::size_t size) noexcept = 0;
    virtual Status start_updates() noexcept = 0;
    // Copy the latest frame without waiting; poll surfaces asynchronous failures.
    virtual Status submit(const OledFrame& frame) noexcept = 0;
    virtual Status poll() noexcept = 0;
    // Join pending transfers before synchronous writes or resource release.
    virtual void stop_updates() noexcept = 0;
    // Must release partially acquired resources; safe to repeat.
    virtual void deinit() noexcept = 0;
};

enum class VoltageKind { ac, dc, average };
// Full-height digits with ~ / + prefix; unavailable data retains the prefix.
OledFrame render_voltage(double volts, bool valid, VoltageKind kind = VoltageKind::ac) noexcept;
std::array<char, 24> format_battery_line(double volts, bool valid, const BatteryEstimate& estimate) noexcept;
OledFrame render_battery(double volts, bool valid, const BatteryEstimate& estimate) noexcept;
OledFrame render_startup_splash() noexcept;
OledFrame render_mains_period(const MainsPeriod& period, bool previous) noexcept;
OledFrame render_relay(bool on, bool valid) noexcept;
// All-zero address means unavailable. Octets are in display order.
OledFrame render_ipv4(const std::array<std::uint8_t, 4>& address) noexcept;

class OledModule final : public Module, public MainsSink, public BatterySink {
public:
    static constexpr unsigned max_pages = 8;
    explicit OledModule(OledTransport& transport, std::uint8_t contrast, unsigned page_count = 1,
                                                std::uint32_t splash_ms = 0, bool show_history = false,
                                                bool show_average = false, bool show_relay = false,
                                                bool show_ip = false) noexcept
        : transport_(transport), contrast_(contrast), page_count_(page_count), splash_ms_(splash_ms),
                    show_history_(show_history), show_average_(show_average), show_relay_(show_relay), show_ip_(show_ip) {}
    const char* id() const noexcept override { return "oled"; }
    // Runtime task only. Copies a reading; does not access I2C.
    void report(const MainsReading& reading) noexcept override;
    // Explicit ordered voltage pages; page 0 is mains. Runtime task only.
    Status report_value(unsigned page, double volts, bool valid) noexcept;
    // Confirmed relay snapshot, copied by the Runtime task without I/O.
    void report_relay(bool on, bool valid) noexcept { relay_on_ = on; relay_valid_ = valid; }
    // Snapshot copied by the owner task, never from network callbacks.
    void report_ipv4(std::array<std::uint8_t, 4> address) noexcept { ipv4_ = address; }
    void report_battery(const BatteryEstimate& estimate) noexcept override {
        battery_ = estimate; battery_enabled_ = true; battery_fresh_ = true;
    }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;

private:
    OledTransport& transport_;
    const std::uint8_t contrast_;
    struct Page {
        double volts = 0;
        std::uint64_t last_reading_ms = 0;
        bool valid = false, fresh = false, received = false;
    };
    const unsigned page_count_;
    const std::uint32_t splash_ms_;
    const bool show_history_;
    const bool show_average_;
    const bool show_relay_;
    const bool show_ip_;
    std::array<std::uint8_t, 4> ipv4_{};
    bool relay_on_ = false, relay_valid_ = false;
    MainsHistory history_{};
    BatteryEstimate battery_{};
    bool battery_enabled_ = false, battery_fresh_ = false;
    std::uint64_t battery_at_ = 0;
    std::array<double, 60> average_samples_{};
    double average_sum_ = 0;
    unsigned average_count_ = 0, average_next_ = 0;
    std::uint64_t splash_started_ms_ = 0;
    bool splash_active_ = false, splash_timed_ = false;
    std::array<Page, max_pages> pages_{};
    OledFrame frame_{};
    std::uint64_t page_since_ms_ = 0;
    unsigned active_page_ = 0;
    bool page_timed_ = false;
    bool acquired_ = false;
    bool initialized_ = false;
    bool present_ = false;
    bool start_attempted_ = false;
    bool running_ = false;
};

}  // namespace bth
