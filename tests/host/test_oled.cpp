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
    int releases = 0;
    bool updating = false, fail_start = false, fail_submit = false;
    Status update_status = Status::ok;
    std::vector<bth::OledFrame> frames;
    std::vector<bth::OledUpdate> updates;
    Status init() noexcept override {
        CHECK_OLED(!acquired);
        acquired = true;
        return fail_init ? Status::hardware_error : Status::ok;
    }
    Status start_updates() noexcept override {
        CHECK_OLED(acquired && !updating);
        if (fail_start) return Status::no_memory;
        updating = true;
        update_status = Status::ok;
        return Status::ok;
    }
    Status submit(const bth::OledUpdate& update) noexcept override {
        CHECK_OLED(updating);
        if (fail_submit) return Status::internal_error;
        if (update_status != Status::ok && update_status != Status::hardware_error) return update_status;
        frames.push_back(update.frame);
        updates.push_back(update);
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
        bth::OledModule oled(bus, 0x8f, 2, 0, true, true, true, true);
        CHECK_OLED(oled.init() == Status::ok && oled.start() == Status::ok);
        bth::MainsReading reading{};
        reading.valid = reading.calibrated = true;
        reading.volts = 230;
        oled.report(reading);
        CHECK_OLED(oled.report_value(1, 12.4, true) == Status::ok);
        oled.report_battery({725, true, true, false});
        oled.report_relay(true, true);
        oled.report_ipv4({192, 168, 1, 12});
        oled.report_outages({bth::OutageDisplayState::interval, 2, 480, 600});
        CHECK_OLED(oled.tick(1000) == Status::ok);
        auto copy = oled.readings(1000);
        CHECK_OLED(copy.average_valid && copy.average_volts == 230);
        CHECK_OLED(copy.battery.valid && copy.battery.minutes == 725);
        CHECK_OLED(copy.relay_valid && copy.relay_on && copy.ipv4[3] == 12);
        CHECK_OLED(copy.outages.group == 2 && copy.outages.end == 600);
        reading.volts = 0;
        oled.report(reading);
        CHECK_OLED(oled.tick(2000) == Status::ok);
        copy = oled.readings(2000);
        CHECK_OLED(copy.average_valid && copy.average_volts == 230);
        CHECK_OLED(!oled.readings(3500).battery.valid);
        CHECK_OLED(!oled.readings(4500).average_valid);
        CHECK_OLED(!oled.readings(999).average_valid);
        oled.stop();
        CHECK_OLED(!oled.readings(2000).relay_valid);
        oled.deinit();
    }
    // Dynamic schedule page disappears immediately when the group is disabled.
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f);
        CHECK_OLED(oled.init() == Status::ok && oled.start() == Status::ok);
        const bth::OutageDisplay value{bth::OutageDisplayState::interval, 12, 480, 1440};
        oled.report_outages(value);
        CHECK_OLED(oled.tick(0) == Status::ok);
        CHECK_OLED(oled.tick(3000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_outages(value));
        oled.report_outages({});
        CHECK_OLED(oled.tick(3020) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false));
        oled.deinit();
    }
    // Every page, including unavailable and longest values, stays in the
    // 105 columns starting at x=17 and clears both covered margins.
    const bth::OledFrame window_frames[] = {
        bth::render_outages({bth::OutageDisplayState::interval,12,1439,1440}),
        bth::render_outages({bth::OutageDisplayState::clear,2,0,0}),
        bth::render_outages({bth::OutageDisplayState::stale,2,0,0}),
        bth::render_outages({bth::OutageDisplayState::unavailable,2,0,0}),
        bth::render_voltage(888.8, true), bth::render_voltage(0, false),
        bth::render_voltage(3.5, true, bth::VoltageKind::dc),
        bth::render_voltage(230, true, bth::VoltageKind::average),
        bth::render_startup_splash(), bth::render_relay(true, true),
        bth::render_relay(false, true), bth::render_relay(false, false),
        bth::render_ipv4({255,255,255,255}), bth::render_ipv4({}),
        bth::render_mains_period({86399999999ULL,true,true}, false),
        bth::render_mains_period({}, true),
        bth::render_battery(12.8, true, {7200,true,true,false}),
        bth::render_battery(0, false, {})
    };
    for (const auto& rendered : window_frames) {
        bool left_lit = false;
        for (int y = 0; y < 32; ++y) {
            left_lit |= pixel(rendered, 17, y);
            for (int x = 0; x < 128; ++x)
                if (x < 17 || x >= 122) CHECK_OLED(!pixel(rendered, x, y));
        }
        CHECK_OLED(left_lit);
    }
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
            CHECK_OLED(!pixel(longest,122,y) && !pixel(longest,127,y));
        // Every octet remains visible within the frame.
        for (int octet = 0; octet < 4; ++octet) {
            bool lit = false;
            for (int x = 17+octet*31*105/128; x < 17+(octet*31+25)*105/128; ++x)
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
            CHECK_OLED(!pixel(longest,122,y) && !pixel(longest,127,y));
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
        CHECK_OLED(pixel(one_hour, 52,0));
        CHECK_OLED(pixel(one_hour, 55,17) && pixel(one_hour, 55,25));
        CHECK_OLED(!pixel(bth::render_mains_period({3600000,false,true},false),52,0));
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
        bth::OledModule oled(bus, 0x8f, 1, 0, false, true);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        bth::MainsReading reading{};
        reading.valid = reading.calibrated = true;
        reading.volts = 230; oled.report(reading);
        CHECK_OLED(runtime.tick(0) == Status::ok);
        reading.volts = 240; oled.report(reading);
        CHECK_OLED(runtime.tick(1000) == Status::ok);
        // More than a full averaging window of outage samples must not dilute it.
        for (unsigned ms = 2000; ms <= 71000; ms += 1000) {
            reading.volts = ms % 2000 == 0 ? 0 : 99.9;
            oled.report(reading);
            CHECK_OLED(runtime.tick(ms) == Status::ok);
            if ((ms / 3000) % 2 == 1)
                CHECK_OLED(bus.frames.back() == bth::render_voltage(235, true, bth::VoltageKind::average));
            else
                CHECK_OLED(bus.frames.back() == bth::render_voltage(reading.volts, true));
        }
        reading.volts = 250; oled.report(reading);
        CHECK_OLED(runtime.tick(72000) == Status::ok);
        reading.volts = 100; oled.report(reading); // Exact presence threshold counts.
        CHECK_OLED(runtime.tick(73000) == Status::ok && runtime.tick(75000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(205, true, bth::VoltageKind::average));
        reading.valid = false; oled.report(reading);
        CHECK_OLED(runtime.tick(76000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false, bth::VoltageKind::average));
        reading.valid = true; reading.volts = 0; oled.report(reading);
        CHECK_OLED(runtime.tick(77000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false, bth::VoltageKind::average));
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok);
        oled.report(reading); CHECK_OLED(runtime.tick(0) == Status::ok);
        oled.report(reading); CHECK_OLED(runtime.tick(3000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(0, false, bth::VoltageKind::average));
    }
    {
        FakeTransport bus;
        bth::OledModule oled(bus, 0x8f, 2, 2000);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        const auto splash = bth::render_startup_splash();
        CHECK_OLED(bus.frames.back() == splash);
        // Text bounds, word space, and the dash's middle stroke.
        CHECK_OLED(pixel(splash, 17, 5) && pixel(splash, 93, 25));
        CHECK_OLED(pixel(splash, 26, 14) && !pixel(splash, 26, 5));
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 128; ++x)
                if (x < 17 || x > 93 || y < 5 || y > 25 || (x >= 47 && x <= 53))
                    CHECK_OLED(!pixel(splash, x, y));
        const auto base = 1ULL << 32;
        CHECK_OLED(runtime.tick(base) == Status::ok);
        CHECK_OLED(oled.report_value(0, 230.5, true) == Status::ok);
        CHECK_OLED(oled.report_value(1, 12.4, true) == Status::ok);
        CHECK_OLED(runtime.tick(base + 1999) == Status::ok && bus.frames.size() == 1);
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
        CHECK_OLED(bus.frames.back() == splash);
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
    const auto frame = bth::render_voltage(230.5, true);
    const auto unavailable = bth::render_voltage(0, false);
    // Check window coverage and the decimal point.
    int min_x = 128, max_x = -1, min_y = 32, max_y = -1;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (!pixel(frame, x, y)) { continue; }
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
    }
    CHECK_OLED(min_x == 17 && max_x == 121 && min_y == 0 && max_y == 31);
    CHECK_OLED(pixel(frame, 84, 28) && pixel(frame, 86, 31));
    CHECK_OLED(!pixel(frame, 84, 27) && !pixel(frame, 87, 31));
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
            CHECK_OLED(pixel(frame, 17 + (origins[digit] + xs[segment]) * 105 / 128, ys[segment]) == expected[digit][segment]);
        }
    }
    CHECK_OLED(pixel(frame, 108, 0) && pixel(frame, 121, 0));
    CHECK_OLED(!pixel(frame, 114, 0) && pixel(frame, 114, 31));
    for (double value : {-1.0, 999.95, 1000.0, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        CHECK_OLED(bth::render_voltage(value, true) == unavailable);
    }
    CHECK_OLED(bth::render_voltage(230.54, true) == frame);
    CHECK_OLED(bth::render_voltage(229.96, true) == bth::render_voltage(230.0, true));
    CHECK_OLED(bth::render_voltage(999.94, true) != unavailable);
    const auto zero = bth::render_voltage(0, true);
    CHECK_OLED(pixel(zero, 33, 0) && !pixel(zero, 33, 15));
    CHECK_OLED(pixel(frame, 18, 12) && pixel(frame, 23, 18) && !pixel(frame, 21, 10));
    const auto battery = bth::render_voltage(12.4, true, bth::VoltageKind::dc);
    CHECK_OLED(pixel(battery, 21, 10) && pixel(battery, 17, 15) && pixel(battery, 26, 17));
    CHECK_OLED(!pixel(battery, 17, 10));
    const auto small_battery = bth::render_voltage(3.5, true, bth::VoltageKind::dc);
    CHECK_OLED(pixel(small_battery, 21, 10) && pixel(small_battery, 17, 15));
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
        CHECK_OLED(bus.frames.size() == 1); // Only the initial frame; report performs no I/O.
        CHECK_OLED(runtime.tick(1000) == Status::ok);
        CHECK_OLED(bus.frames.size() == 2 && bus.frames.back() == frame);
        CHECK_OLED(runtime.tick(1020) == Status::ok && bus.frames.size() == 2);
        reading.volts = 231.2;
        oled.report(reading);
        CHECK_OLED(runtime.tick(2000) == Status::ok);
        CHECK_OLED(bus.frames.back() == bth::render_voltage(231.2, true));
        CHECK_OLED(runtime.tick(4499) == Status::ok && bus.frames.size() == 4);
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
        runtime.stop();
        CHECK_OLED(runtime.start() == Status::ok);
        const auto count = bus.frames.size();
        CHECK_OLED(runtime.tick(0) == Status::ok && bus.frames.size() == count);
        bus.update_status = Status::hardware_error;
        CHECK_OLED(runtime.tick(20) == Status::ok);
        CHECK_OLED(bus.updating && bus.acquired && runtime.state() == bth::RuntimeState::running);
        CHECK_OLED(runtime.tick(2999) == Status::ok && bus.frames.size() == count);
        CHECK_OLED(runtime.tick(3000) == Status::ok && bus.frames.size() == count+1);
        CHECK_OLED(bus.updates.back().generation == 1);
        bus.update_status = Status::ok;
        CHECK_OLED(runtime.tick(3020) == Status::ok);
        bus.update_status = Status::internal_error;
        CHECK_OLED(runtime.tick(3040) == Status::internal_error);
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
        CHECK_OLED(oled.start() == Status::invalid_state && oled.tick(0) == Status::invalid_state);
        oled.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 0);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok && runtime.start() == Status::ok);
        CHECK_OLED(oled.init() == Status::invalid_state && oled.start() == Status::invalid_state);
        CHECK_OLED(bus.updates.size() == 1 && bus.updates.back().generation == 0);
        CHECK_OLED(bus.updates.back().contrast == 0x8f && bus.frames.back() == unavailable);
        CHECK_OLED(runtime.tick(0) == Status::ok && runtime.tick(20) == Status::ok);
        CHECK_OLED(bus.updates.size() == 1);
        CHECK_OLED(runtime.tick(3000) == Status::ok && bus.updates.size() == 2);
        CHECK_OLED(bus.updates.back().generation == 1 && bus.frames.back() == unavailable);
        CHECK_OLED(runtime.tick(100000) == Status::ok && bus.updates.size() == 3);
        runtime.stop(); runtime.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 1 && !bus.acquired && !bus.updating);
        CHECK_OLED(runtime.start() == Status::ok && bus.updates.back().generation == 0);
    }
    for (int failure = 0; failure < 3; ++failure) {
        FakeTransport bus;
        bus.fail_init = failure == 0;
        bus.fail_start = failure == 1;
        bus.fail_submit = failure == 2;
        bth::OledModule oled(bus, 0x8f);
        bth::Runtime runtime;
        CHECK_OLED(runtime.add(oled) == Status::ok);
        CHECK_OLED(runtime.start() != Status::ok);
        CHECK_OLED(!bus.acquired && !bus.updating && bus.releases == 1);
        runtime.stop(); oled.deinit();
        CHECK_OLED(bus.releases == 1);
        bus.fail_init = bus.fail_start = bus.fail_submit = false;
        CHECK_OLED(runtime.start() == Status::ok);
    }
}
