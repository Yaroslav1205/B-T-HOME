#include "bth/battery/battery.hpp"
#include "bth/core/runtime.hpp"
#include "bth/oled/oled.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
#define CHECK_BATTERY(x) do { if (!(x)) { std::cerr << "Battery line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
using bth::Status;
constexpr std::uint64_t full = 12000ULL * 3600000;
struct Store : bth::BatteryStore {
    std::uint64_t charge = full;
    bool found = false, opened = false;
    int writes = 0, closes = 0;
    Status init_result = Status::ok, load_result = Status::ok, save_result = Status::ok;
    Status init() noexcept override { opened = true; return init_result; }
    Status load(std::uint64_t& value, bool& exists) noexcept override {
        value = charge; exists = found; return load_result;
    }
    Status save(std::uint64_t value) noexcept override {
        ++writes;
        if (save_result == Status::ok) { charge = value; found = true; }
        return save_result;
    }
    void deinit() noexcept override { if (opened) ++closes; opened = false; }
};
struct Sink : bth::BatterySink {
    bth::BatteryEstimate value;
    void report_battery(const bth::BatteryEstimate& estimate) noexcept override { value = estimate; }
};
void reports(bth::BatteryModule& battery, bool mains, double dc = 12.8, bool valid = true) {
    bth::MainsReading ac{};
    ac.volts = mains ? 230 : 0;
    ac.valid = ac.calibrated = valid;
    ac.history.current.known = valid; ac.history.current.present = mains;
    battery.report(ac);
    bth::DcReading reading{}; reading.volts = dc; reading.valid = valid;
    battery.report(reading);
}
}
void test_battery() {
    {
        Store store; Sink sink;
        bth::BatteryModule battery(store, sink, {12000, 100000, 1500, 10.0});
        bth::Runtime runtime;
        CHECK_BATTERY(runtime.add(battery) == Status::ok && runtime.start() == Status::ok);
        CHECK_BATTERY(store.writes == 1 && store.charge == full);
        CHECK_BATTERY(!sink.value.valid);
        const auto base = 1ULL << 33;
        reports(battery, false);
        CHECK_BATTERY(runtime.tick(base) == Status::ok && sink.value.minutes == 7200);
        for (std::uint64_t ms = 1000; ms <= 36000000; ms += 1000) {
            reports(battery, false);
            CHECK_BATTERY(runtime.tick(base + ms) == Status::ok);
        }
        // Ten hours at 100 mA consumed exactly 1 Ah, with no NVS writes in tick.
        CHECK_BATTERY(sink.value.valid && sink.value.minutes == 6600 && store.writes == 1);
        CHECK_BATTERY(battery.service() == Status::ok && store.writes == 2);
        CHECK_BATTERY(store.charge == 11000ULL * 3600000);
        reports(battery, true);
        CHECK_BATTERY(runtime.tick(base + 36000000) == Status::ok);
        for (std::uint64_t ms = 1000; ms <= 2400000; ms += 1000) {
            reports(battery, true);
            CHECK_BATTERY(runtime.tick(base + 36000000 + ms) == Status::ok);
        }
        CHECK_BATTERY(sink.value.minutes == 7200 && sink.value.mains_present);
        reports(battery, true);
        CHECK_BATTERY(runtime.tick(base + 38401000) == Status::ok && sink.value.minutes == 7200);
        runtime.stop(); runtime.stop(); battery.deinit();
        CHECK_BATTERY(store.charge == full && store.closes == 1);
        CHECK_BATTERY(runtime.start() == Status::ok);
        reports(battery, false);
        CHECK_BATTERY(runtime.tick(0) == Status::ok && sink.value.minutes == 7200);
    }
    {
        Store store; store.found = true; store.charge = 6000ULL * 3600000;
        Sink sink; bth::BatteryModule battery(store, sink, {12000, 100000, 1500, 10.0});
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 3600);
        for (std::uint64_t now = 1000; now <= 300000; now += 1000) {
            reports(battery, false); CHECK_BATTERY(battery.tick(now) == Status::ok);
            if (now < 300000) CHECK_BATTERY(battery.service() == Status::ok && store.writes == 0);
        }
        CHECK_BATTERY(battery.service() == Status::ok && store.writes == 1);
        CHECK_BATTERY(sink.value.minutes == 3595);
        // A reboot loads the partial charge instead of resetting to full.
        battery.deinit();
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false); CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 3595);
        CHECK_BATTERY(battery.tick(2500) == Status::ok && !sink.value.valid && !sink.value.mains_known);
        reports(battery, true, 13.8, false);
        CHECK_BATTERY(battery.tick(3600000) == Status::ok && !sink.value.valid);
        reports(battery, true);
        CHECK_BATTERY(battery.tick(7200000) == Status::ok && sink.value.minutes == 3595);
        // Backwards clocks and long scheduler gaps must not invent charge.
        reports(battery, true);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 3595);
        reports(battery, true);
        CHECK_BATTERY(battery.tick(3600000) == Status::ok && sink.value.minutes == 3595);
        reports(battery, false, 10.0);
        CHECK_BATTERY(battery.tick(3600000) == Status::ok && sink.value.minutes == 0);
        CHECK_BATTERY(battery.service() == Status::ok && store.charge == 0);
        // Voltage rebound cannot refill an empty battery.
        reports(battery, false, 12.0);
        CHECK_BATTERY(battery.tick(3601000) == Status::ok && sink.value.minutes == 0);
        battery.deinit();
    }
    for (int failure = 0; failure < 4; ++failure) {
        Store store; Sink sink;
        if (failure == 0) store.init_result = Status::hardware_error;
        if (failure == 1) store.load_result = Status::hardware_error;
        if (failure == 2) store.save_result = Status::hardware_error;
        if (failure == 3) { store.found = true; store.charge = full + 1; }
        bth::BatteryModule battery(store, sink, {12000, 100000, 1500, 10.0});
        bth::Runtime runtime;
        CHECK_BATTERY(runtime.add(battery) == Status::ok && runtime.start() != Status::ok);
        CHECK_BATTERY(!store.opened && store.closes == 1);
        battery.deinit(); CHECK_BATTERY(store.closes == 1);
        store.init_result = store.load_result = store.save_result = Status::ok;
        store.charge = full;
        runtime.stop();
        CHECK_BATTERY(runtime.start() == Status::ok);
    }
    {
        Store store; Sink sink; bth::BatteryModule battery(store, sink, {12000, 100000, 1500, 10.0});
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false, 9.9); CHECK_BATTERY(battery.tick(0) == Status::ok);
        store.save_result = Status::hardware_error;
        CHECK_BATTERY(battery.service() == Status::hardware_error);
        CHECK_BATTERY(battery.service() == Status::invalid_state);
        const auto attempts = store.writes;
        battery.deinit(); CHECK_BATTERY(store.writes == attempts && store.closes == 1);
    }
    {
        Store store; Sink sink;
        bth::BatteryModule battery(store, sink, {12000, 0, 1500, 10});
        CHECK_BATTERY(battery.init() == Status::invalid_argument && !store.opened);
        CHECK_BATTERY(battery.start() == Status::invalid_state);
        CHECK_BATTERY(battery.tick(0) == Status::invalid_state);
        CHECK_BATTERY(battery.service() == Status::invalid_state);
        battery.deinit();
    }
    {
        // User's corrected current is 58 mA at 12.8 V (58000 uA).
        // Existing NVS stores charge, so changing current must not refill the battery.
        Store store; store.found = true; store.charge = 6000ULL * 3600000;
        Sink sink; bth::BatteryModule battery(store, sink, {});
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false, 12.8);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 6206);
        CHECK_BATTERY(store.writes == 0);
        // Verify the integrated charge at the application scheduling interval.
        for (std::uint64_t now = 20; now <= 3600000; now += 20) {
            reports(battery, false, 12.8);
            CHECK_BATTERY(battery.tick(now) == Status::ok);
        }
        CHECK_BATTERY(sink.value.minutes == 6146);
        CHECK_BATTERY(battery.service() == Status::ok);
        CHECK_BATTERY(store.charge == 6000ULL * 3600000 - 208800000);
        battery.deinit();
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false, 12.8);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 6146);
        reports(battery, false, 10.0);
        CHECK_BATTERY(battery.tick(20) == Status::ok && sink.value.minutes == 0);
        CHECK_BATTERY(battery.service() == Status::ok && store.charge == 0);
        battery.deinit();
    }
    {
        Store store; Sink sink; bth::BatteryModule battery(store, sink, {});
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false, 12.8);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.minutes == 12413);
        CHECK_BATTERY(store.charge == full);
        battery.deinit();
    }
    {
        // The actual battery range is 10.5-14.3 V. Voltage alone never refills it.
        Store store; store.found = true; store.charge = 6000ULL * 3600000;
        Sink sink; bth::BatteryModule battery(store, sink, {});
        CHECK_BATTERY(battery.init() == Status::ok && battery.start() == Status::ok);
        reports(battery, false, 10.5001);
        CHECK_BATTERY(battery.tick(0) == Status::ok && sink.value.valid && sink.value.minutes == 6206);
        reports(battery, false, 10.5, false);
        CHECK_BATTERY(battery.tick(20) == Status::ok && !sink.value.valid);
        reports(battery, true, 14.3);
        CHECK_BATTERY(battery.tick(40) == Status::ok && sink.value.minutes == 6206);
        CHECK_BATTERY(battery.service() == Status::ok && store.writes == 0);
        // A fresh boundary reading without mains depletes and checkpoints immediately.
        reports(battery, false, 10.5);
        CHECK_BATTERY(battery.tick(60) == Status::ok && sink.value.valid && sink.value.minutes == 0);
        CHECK_BATTERY(battery.service() == Status::ok && store.charge == 0);
        // Recovery to 14.3 V on mains adds only the modeled charge: 25 mAh per minute.
        reports(battery, true, 14.3);
        CHECK_BATTERY(battery.tick(80) == Status::ok && sink.value.minutes == 0);
        for (std::uint64_t now = 1080; now <= 60080; now += 1000) {
            reports(battery, true, 14.3);
            CHECK_BATTERY(battery.tick(now) == Status::ok);
        }
        CHECK_BATTERY(sink.value.minutes == 25);
        battery.deinit();
        CHECK_BATTERY(store.charge == 25ULL * 3600000);
    }
    const auto text = [](double volts, bool valid, bth::BatteryEstimate value, const char* expected) {
        CHECK_BATTERY(std::strcmp(bth::format_battery_line(volts, valid, value).data(), expected) == 0);
    };
    text(11.7, true, {725, true, true, false}, "-11.7V=12:05");
    text(12.8, true, {7200, true, true, false}, "-12.8V=>99:59");
    text(12.8, true, {5999, true, true, false}, "-12.8V=99:59");
    text(12.8, true, {59, true, true, false}, "-12.8V=00:59");
    text(12.8, true, {60, true, true, false}, "-12.8V=01:00");
    text(10.0, true, {0, true, true, false}, "-10.0V=00:00");
    text(11.7, true, {0, false, true, false}, "-11.7V=--:--");
    text(11.7, true, {}, "?11.7V=--:--");
    text(11.7, false, {725, true, true, false}, "---.-V=--:--");
    text(std::numeric_limits<double>::quiet_NaN(), true, {}, "?--.-V=--:--");
}
