#include "bth/oled/oled.hpp"
#include "bth/core/runtime.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
#define CHECK_OLED(c) do { if (!(c)) { std::cerr << "oled:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct FakeTransport final : bth::OledTransport {
    bool acquired = false;
    bool fail_init = false;
    bool present = true;
    bool fail_probe = false;
    int probes = 0;
    int fail_write = -1;
    int releases = 0;
    bool updating = false, fail_start = false;
    Status update_status = Status::ok;
    std::vector<bth::OledFrame> frames;
    std::vector<std::vector<std::uint8_t>> writes;
    Status init() noexcept override {
        CHECK_OLED(!acquired);
        acquired = true;
        return fail_init ? Status::hardware_error : Status::ok;
    }
    Status probe(bool& found) noexcept override {
        CHECK_OLED(acquired);
        ++probes;
        found = present;
        return fail_probe ? Status::hardware_error : Status::ok;
    }
    Status write(const std::uint8_t* data, std::size_t size) noexcept override {
        CHECK_OLED(acquired && !updating);
        writes.emplace_back(data, data + size);
        return static_cast<int>(writes.size()) == fail_write ? Status::hardware_error : Status::ok;
    }
    Status start_updates() noexcept override {
        CHECK_OLED(acquired && !updating);
        if (fail_start) return Status::no_memory;
        updating = true;
        return Status::ok;
    }
    Status submit(const bth::OledFrame& frame) noexcept override {
        CHECK_OLED(updating);
        if (update_status != Status::ok) return update_status;
        frames.push_back(frame);
        return Status::ok;
    }
    Status poll() noexcept override { CHECK_OLED(updating); return update_status; }
    void stop_updates() noexcept override { updating = false; }
    void deinit() noexcept override { CHECK_OLED(!updating); acquired = false; ++releases; }
};
bool pixel(const bth::OledFrame& frame, int x, int y) {
    return (frame[static_cast<std::size_t>(y / 8 * 128 + x)] & (1U << (y % 8))) != 0;
}
}

void test_oled() {
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2, 2000, true, true, true, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        CHECK_OLED(runtime.tick(0) == Status::ok && runtime.tick(2000) == Status::ok);
        CHECK_OLED(runtime.tick(17000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(false, false));
        CHECK_OLED(runtime.tick(20000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_ipv4({}));
        oled.report_ipv4({192,168,1,123});
        CHECK_OLED(runtime.tick(20020) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_ipv4({192,168,1,123}));
        const auto frames = bus.frames.size();
        CHECK_OLED(runtime.tick(20040) == Status::ok && frames == bus.frames.size());
        // Lost connection clears the displayed lease; reconnect can change it.
        oled.report_ipv4({});
        CHECK_OLED(runtime.tick(21000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_ipv4({}));
        oled.report_ipv4({10,0,0,5});
        CHECK_OLED(runtime.tick(22000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_ipv4({10,0,0,5}));
        CHECK_OLED(runtime.tick(23000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok && runtime.tick(0) == Status::ok);
        CHECK_OLED(runtime.tick(2000) == Status::ok && runtime.tick(20000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_ipv4({}));
        const auto longest = bth::render_ipv4({255,255,255,255});
        CHECK_OLED(longest != bth::render_ipv4({}) && longest != bth::render_ipv4({192,168,1,123}));
        for (int y = 0; y < 32; ++y)
            CHECK_OLED(!pixel(longest,0,y) && !pixel(longest,127,y));
        // Every octet remains visible within the frame.
        for (int octet = 0; octet < 4; ++octet) {
            bool lit = false;
            for (int x = 4+octet*31; x < 4+octet*31+25; ++x)
                for (int y = 11; y < 29; ++y) lit = lit || pixel(longest,x,y);
            CHECK_OLED(lit);
        }
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bth::BatteryEstimate estimate{725, true, true, false};
        for (unsigned now = 0; now <= 3000; now += 1000) {
            oled.report_battery(estimate);
            CHECK_OLED(oled.report_value(1, 11.7, true) == Status::ok);
            CHECK_OLED(runtime.tick(now) == Status::ok);
        }
        const auto remaining = bth::render_battery(11.7, true, estimate);
        CHECK_OLED(bus.frames.back() == remaining);
        CHECK_OLED(remaining != bth::render_voltage(11.7, true, bth::VoltageKind::dc));
        estimate.mains_present = true;
        oled.report_battery(estimate);
        CHECK_OLED(runtime.tick(3020) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(11.7, true, bth::VoltageKind::dc));
        // Expired state must not keep a plausible time or a charging sign.
        CHECK_OLED(oled.report_value(1, 11.7, true) == Status::ok);
        CHECK_OLED(runtime.tick(5520) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_battery(11.7, true, {}));
        CHECK_OLED(bth::render_battery(12.8, true, {7200,true,true,false}) !=
                   bth::render_battery(12.8, true, {5999,true,true,false}));
        // Compact text keeps padding and does not clip the 128 x 32 frame.
        const auto longest = bth::render_battery(12.8, true, {7200,true,true,false});
        for (int x = 0; x < 128; ++x) {
            CHECK_OLED(!pixel(longest,x,0) && !pixel(longest,x,31));
        }
        for (int y = 0; y < 32; ++y) {
            CHECK_OLED(!pixel(longest,0,y) && !pixel(longest,127,y));
        }
    }

    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2, 2000, true, true, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        CHECK_OLED(runtime.tick(0) == Status::ok);
        CHECK_OLED(runtime.tick(2000) == Status::ok);
        CHECK_OLED(runtime.tick(17000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(false, false));
        oled.report_relay(false, true);
        CHECK_OLED(runtime.tick(17020) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(false, true));
        oled.report_relay(true, true);
        CHECK_OLED(runtime.tick(17040) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(true, true));
        CHECK_OLED(bth::render_relay(true, true) != bth::render_relay(false, true));
        CHECK_OLED(bth::render_relay(false, false) != bth::render_relay(false, true));
        const auto count = bus.frames.size();
        CHECK_OLED(runtime.tick(19999) == Status::ok && bus.frames.size() == count);
        CHECK_OLED(runtime.tick(20000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok && runtime.tick(0) == Status::ok);
        CHECK_OLED(runtime.tick(2000) == Status::ok && runtime.tick(17000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(false, false));
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 1, 0, false, false, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        oled.report_relay(true, true);
        CHECK_OLED(runtime.tick(0) == Status::ok && runtime.tick(3000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(true, true));
        oled.report_relay(true, false);
        CHECK_OLED(runtime.tick(3020) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_relay(false, false));
        CHECK_OLED(runtime.tick(6000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2, 0, true, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bth::MainsReading reading{};
        reading.valid = reading.calibrated = true; reading.volts = 230;
        reading.history.current = {120000, true, true};
        reading.history.previous = {7200000, false, true};
        for (unsigned ms = 0; ms <= 15000; ms += 1000) {
            reading.volts = ms == 0 ? 230 : 240;
            oled.report(reading);
            CHECK_OLED(oled.report_value(1, 12.4, true) == Status::ok);
            CHECK_OLED(runtime.tick(ms) == Status::ok);
            if (ms == 0) CHECK_OLED(bus.frames.back() == bth::render_voltage(230, true));
            if (ms == 3000) CHECK_OLED(bus.frames.back() == bth::render_voltage(237.5, true, bth::VoltageKind::average));
            if (ms == 6000) CHECK_OLED(bus.frames.back() == bth::render_voltage(12.4, true, bth::VoltageKind::dc));
            if (ms == 9000) CHECK_OLED(bus.frames.back() == bth::render_mains_period(reading.history.current, false));
            if (ms == 12000) CHECK_OLED(bus.frames.back() == bth::render_mains_period(reading.history.previous, true));
            if (ms == 15000) CHECK_OLED(bus.frames.back() == bth::render_voltage(240, true));
        }
        CHECK_OLED(runtime.tick(18000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false, bth::VoltageKind::average));
        const auto under_hour = bth::render_mains_period({3599999,true,true}, false);
        const auto one_hour = bth::render_mains_period({3600000,true,true}, false);
        CHECK_OLED(under_hour != one_hour);
        CHECK_OLED(pixel(one_hour,43,0));
        CHECK_OLED(pixel(one_hour,47,17) && pixel(one_hour,47,25));
        CHECK_OLED(!pixel(bth::render_mains_period({3600000,false,true},false),43,0));
        CHECK_OLED(one_hour != bth::render_mains_period({3600000,true,true},true));
        constexpr std::uint64_t max_duration_ms = (999ULL * 24 * 60 + 1439) * 60000;
        CHECK_OLED(bth::render_mains_period({max_duration_ms,true,true}, false) ==
               bth::render_mains_period({max_duration_ms + 60000,true,true}, false));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok && runtime.tick(0) == Status::ok);
        CHECK_OLED(runtime.tick(9000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_mains_period({}, false));
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 1, 0, false, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bth::MainsReading reading{};
        reading.valid = reading.calibrated = true;
        for (unsigned value = 100; value < 160; ++value) {
            reading.volts = value;
            oled.report(reading);
        }
        reading.volts = 160;
        oled.report(reading);
        CHECK_OLED(runtime.tick(0) == Status::ok);
        oled.report(reading);
        CHECK_OLED(runtime.tick(1000) == Status::ok);
        CHECK_OLED(runtime.tick(3000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(131.5, true, bth::VoltageKind::average));
        reading.valid = false;
        oled.report(reading);
        CHECK_OLED(runtime.tick(4000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false, bth::VoltageKind::average));
        runtime.stop();
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2, 2000);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        const auto splash = bth::render_startup_splash();
        CHECK_OLED(std::equal(splash.begin(), splash.end(), bus.writes[1].begin() + 1));
        // Text bounds, word space, and the dash's middle stroke.
        CHECK_OLED(pixel(splash, 17, 5) && pixel(splash, 110, 25));
        CHECK_OLED(pixel(splash, 29, 14) && !pixel(splash, 29, 5));
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 128; ++x)
                if (x < 17 || x > 110 || y < 5 || y > 25 || (x >= 53 && x <= 62))
                    CHECK_OLED(!pixel(splash, x, y));
        const auto base = 1ULL << 32;
        CHECK_OLED(runtime.tick(base) == Status::ok);
        CHECK_OLED(oled.report_value(0, 230.5, true) == Status::ok);
        CHECK_OLED(oled.report_value(1, 12.4, true) == Status::ok);
        CHECK_OLED(runtime.tick(base + 1999) == Status::ok && bus.frames.empty());
        CHECK_OLED(runtime.tick(base + 2000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(230.5, true));
        CHECK_OLED(oled.report_value(0, 231, true) == Status::ok);
        CHECK_OLED(oled.report_value(1, 12.5, true) == Status::ok);
        CHECK_OLED(runtime.tick(base + 4999) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(231, true));
        CHECK_OLED(runtime.tick(base + 5000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(12.5, true, bth::VoltageKind::dc));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(std::equal(splash.begin(), splash.end(), bus.writes[bus.writes.size() - 2].begin() + 1));
        const auto count = bus.frames.size();
        CHECK_OLED(runtime.tick(0) == Status::ok);
        CHECK_OLED(runtime.tick(1999) == Status::ok && bus.frames.size() == count);
        CHECK_OLED(runtime.tick(7000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        const auto base = 1ULL << 32;
        for (unsigned ms = 0; ms <= 9000; ms += 20) {
            if (ms % 1000 == 0) {
                CHECK_OLED(oled.report_value(0, 230.5, true) == Status::ok);
                CHECK_OLED(oled.report_value(1, ms < 6000 ? 3.5 : 29.8, true) == Status::ok);
            }
            CHECK_OLED(runtime.tick(base + ms) == Status::ok);
            const bool dc = (ms / 3000) % 2 == 1;
            const double expected = dc ? (ms < 6000 ? 3.5 : 29.8) : 230.5;
            CHECK_OLED(bus.frames.back() == bth::render_voltage(expected, true,
                dc ? bth::VoltageKind::dc : bth::VoltageKind::ac));
        }
        CHECK_OLED(oled.report_value(2, 12, true) == Status::invalid_argument);
        // Both pages expire independently while rotation continues.
        CHECK_OLED(runtime.tick(base + 12000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
        CHECK_OLED(oled.report_value(1, 12.4, true) == Status::ok);
        CHECK_OLED(runtime.tick(base + 15000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(12.4, true, bth::VoltageKind::dc));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(oled.report_value(0, 220, true) == Status::ok);
        CHECK_OLED(runtime.tick(0) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(220, true));
    }
    for (unsigned pages : {0U, bth::OledModule::max_pages + 1}) {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, pages);
        CHECK_OLED(oled.init() == Status::invalid_argument && !bus.acquired);
        oled.deinit();
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, bth::OledModule::max_pages, 0, false, true);
        CHECK_OLED(oled.init() == Status::invalid_argument && !bus.acquired);
    }
    {
        FakeTransport bus;
        bus.present = false;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(runtime.state() == bth::RuntimeState::running);
        CHECK_OLED(bus.probes == 1 && bus.releases == 1 && !bus.acquired);
        CHECK_OLED(oled.init() == Status::invalid_state);
        CHECK_OLED(oled.start() == Status::invalid_state);
        for (auto ms : {0ULL, 1000ULL, 5000ULL}) CHECK_OLED(runtime.tick(ms) == Status::ok);
        CHECK_OLED(bus.writes.empty() && bus.probes == 1);
        runtime.stop(); runtime.stop(); oled.deinit();
        CHECK_OLED(bus.writes.empty() && bus.releases == 1);
        // Detect a newly attached display on the next startup without configuration changes.
        bus.present = true;
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(bus.probes == 2 && bus.writes.size() == 3);
        runtime.stop();
        bus.writes.clear(); bus.present = false;
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(bus.probes == 3 && bus.writes.empty() && !bus.acquired);
    }
    {
        FakeTransport bus;
        bus.fail_probe = true;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() == Status::hardware_error);
        CHECK_OLED(bus.releases == 1 && !bus.acquired && bus.writes.empty());
        runtime.stop(); bus.fail_probe = false;
        CHECK_OLED(runtime.start() == Status::ok);
    }
    const auto frame = bth::render_voltage(230.5, true);
    const auto unavailable = bth::render_voltage(0, false);
    // Check full display coverage and the decimal point.
    int min_x = 128, max_x = -1, min_y = 32, max_y = -1;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (!pixel(frame, x, y)) { continue; }
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
    }
    CHECK_OLED(min_x == 0 && max_x == 127 && min_y == 0 && max_y == 31);
    CHECK_OLED(pixel(frame, 82, 28) && pixel(frame, 85, 31));
    CHECK_OLED(!pixel(frame, 82, 27) && !pixel(frame, 86, 31));
    // Independent segment expectations for 2, 3, 0, 5, including unlit segments.
    constexpr int origins[] = {16, 38, 60, 90};
    constexpr bool expected[4][7] = {
        {true, true, false, true, true, false, true},
        {true, true, true, true, false, false, true},
        {true, true, true, true, true, true, false},
        {true, false, true, true, false, true, true}};
    constexpr int xs[] = {8, 16, 16, 8, 1, 1, 8};
    constexpr int ys[] = {0, 8, 22, 31, 22, 8, 15};
    for (int digit = 0; digit < 4; ++digit) {
        for (int segment = 0; segment < 7; ++segment) {
            CHECK_OLED(pixel(frame, origins[digit] + xs[segment], ys[segment]) == expected[digit][segment]);
        }
    }
    CHECK_OLED(pixel(frame, 112, 0) && pixel(frame, 127, 0));
    CHECK_OLED(!pixel(frame, 119, 0) && pixel(frame, 119, 31));
    for (double value : {-1.0, 999.95, 1000.0, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        CHECK_OLED(bth::render_voltage(value, true) == unavailable);
    }
    CHECK_OLED(bth::render_voltage(230.54, true) == frame);
    CHECK_OLED(bth::render_voltage(229.96, true) == bth::render_voltage(230.0, true));
    CHECK_OLED(bth::render_voltage(999.94, true) != unavailable);
    const auto zero = bth::render_voltage(0, true);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 44; ++x) CHECK_OLED(!pixel(zero, x, y));
    CHECK_OLED(pixel(zero, 64, 0) && !pixel(zero, 64, 15));
    CHECK_OLED(pixel(frame, 2, 12) && pixel(frame, 8, 18) && !pixel(frame, 6, 10));
    const auto battery = bth::render_voltage(12.4, true, bth::VoltageKind::dc);
    CHECK_OLED(pixel(battery, 27, 10) && pixel(battery, 22, 15) && pixel(battery, 33, 17));
    CHECK_OLED(!pixel(battery, 22, 10));
    const auto small_battery = bth::render_voltage(3.5, true, bth::VoltageKind::dc);
    CHECK_OLED(pixel(small_battery, 49, 10) && pixel(small_battery, 44, 15));
    CHECK_OLED(bth::render_voltage(0, false, bth::VoltageKind::dc) != unavailable);
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bth::MainsReading reading{};
        reading.valid = reading.calibrated = true;
        reading.volts = 230.5;
        oled.report(reading);
        CHECK_OLED(bus.frames.empty()); // report only copies; tick queues the frame.
        CHECK_OLED(runtime.tick(1000) == Status::ok);
        CHECK_OLED(bus.frames.size() == 1 && bus.frames.back() == frame);
        CHECK_OLED(runtime.tick(1020) == Status::ok && bus.frames.size() == 1);
        reading.volts = 231.2;
        oled.report(reading);
        CHECK_OLED(runtime.tick(2000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(231.2, true));
        CHECK_OLED(runtime.tick(4499) == Status::ok && bus.frames.size() == 2);
        CHECK_OLED(runtime.tick(4500) == Status::ok && bus.frames.back() == unavailable);
        oled.report(reading);
        CHECK_OLED(runtime.tick(5000) == Status::ok && bus.frames.back() != unavailable);
        reading.calibrated = false;
        oled.report(reading);
        CHECK_OLED(runtime.tick(6000) == Status::ok && bus.frames.back() == unavailable);
        reading.calibrated = true;
        oled.report(reading);
        CHECK_OLED(runtime.tick(7000) == Status::ok && bus.frames.back() != unavailable);
        reading.valid = false;
        oled.report(reading);
        CHECK_OLED(runtime.tick(8000) == Status::ok && bus.frames.back() == unavailable);
        CHECK_OLED(bus.writes.size() == 3); // No synchronous I2C from tick.
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok);
        const auto count = bus.frames.size();
        CHECK_OLED(runtime.tick(0) == Status::ok && bus.frames.size() == count);
        bus.update_status = Status::hardware_error;
        CHECK_OLED(runtime.tick(20) == Status::hardware_error);
        CHECK_OLED(!bus.updating && !bus.acquired);
    }
    {
        FakeTransport bus;
        bus.fail_start = true;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() == Status::no_memory);
        CHECK_OLED(!bus.updating && !bus.acquired && bus.releases == 1);
        runtime.stop();
        bus.fail_start = false;
        CHECK_OLED(runtime.start() == Status::ok);
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f);
        CHECK_OLED(oled.start() == Status::invalid_state);
        CHECK_OLED(oled.tick(0) == Status::invalid_state);
        oled.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 0);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(oled.init() == Status::invalid_state);
        CHECK_OLED(oled.start() == Status::invalid_state);
        CHECK_OLED(bus.writes.size() == 3);
        const auto& setup = bus.writes[0];
        CHECK_OLED(setup.front() == 0x00 && setup[1] == 0xae);
        const auto has = [&](std::uint8_t command, std::uint8_t value) {
            const std::uint8_t pair[] = {command, value};
            return std::search(setup.begin(), setup.end(), pair, pair + 2) != setup.end();
        };
        CHECK_OLED(has(0xa8, 0x1f) && has(0xda, 0x02));
        CHECK_OLED(has(0x81, 0x8f) && has(0x8d, 0x14) && has(0x20, 0x00));
        const std::vector<std::uint8_t> window{0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
        CHECK_OLED(std::equal(window.begin(), window.end(), setup.end() - 6));
        CHECK_OLED(bus.writes[1].size() == 513 && bus.writes[1][0] == 0x40);
        CHECK_OLED(std::equal(unavailable.begin(), unavailable.end(), bus.writes[1].begin() + 1));
        CHECK_OLED(bus.writes[2] == (std::vector<std::uint8_t>{0x00, 0xaf}));
        for (auto ms : {0ULL, 20ULL, 100000ULL}) { CHECK_OLED(runtime.tick(ms) == Status::ok); }
        CHECK_OLED(bus.writes.size() == 3);
        runtime.stop(); runtime.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 1 && !bus.acquired && bus.writes.size() == 4);
        CHECK_OLED(bus.writes.back() == (std::vector<std::uint8_t>{0x00, 0xae}));
        CHECK_OLED(runtime.start() == Status::ok);
        CHECK_OLED(bus.writes.size() == 7);
    }
    // Partial init and failures at every transfer must release the owned bus once.
    for (int failure = 0; failure <= 3; ++failure) {
        FakeTransport bus;
        bus.fail_init = failure == 0;
        bus.fail_write = failure;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() == Status::hardware_error);
        CHECK_OLED(runtime.state() == bth::RuntimeState::failed);
        CHECK_OLED(!bus.acquired && bus.releases == 1);
        if (failure > 0) { CHECK_OLED(bus.writes.back() == (std::vector<std::uint8_t>{0x00, 0xae})); }
        runtime.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 1);
        bus.fail_init = false; bus.fail_write = -1;
        CHECK_OLED(runtime.start() == Status::ok);
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bus.fail_write = 4;  // A failed display-off must still release the bus.
        runtime.stop();
        CHECK_OLED(!bus.acquired && bus.releases == 1);
    }
}
