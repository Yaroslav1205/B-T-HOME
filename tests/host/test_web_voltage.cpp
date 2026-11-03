#include "bth/web/voltage.hpp"
#include "bth/web/voltage_history.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

void test_web_voltage() {
    const auto check = [](const bth::WebVoltage& value, std::uint64_t now, const char* expected) {
        if (std::strcmp(bth::voltage_status(value, now), expected) != 0) {
            std::cerr << "Unexpected web voltage status: " << expected << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    const auto base = 1ULL << 32;
    bth::WebVoltage value;
    check(value, base, "waiting");
    value = {230.5, base, true, true, true};
    check(value, base, "ok");
    check(value, base + 2499, "ok");
    check(value, base + 2500, "stale");
    check(value, base - 1, "stale");
    value.calibrated = false;
    check(value, base, "uncalibrated");
    value.calibrated = true; value.valid = false;
    check(value, base, "invalid");
    value.valid = true; value.volts = 0;
    check(value, base, "ok");
    for (double volts : {-0.1, 999.95, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
        value.volts = volts;
        check(value, base, "invalid");
    }
    bth::WebVoltages pair;
    pair.mains = {0, base, true, true, false};
    pair.battery = {12.4, base + 1000, true, true, true};
    check(pair.mains, base + 2500, "stale");
    check(pair.battery, base + 2500, "ok");

    const auto require = [](bool condition) {
        if (!condition) { std::cerr << "Voltage history failed\n"; std::exit(EXIT_FAILURE); }
    };
    bth::VoltageHistory history;
    require(history.get(0).count == 0);
    history.record(0, 230, true);
    history.record(999, 900, true); // Duplicate second is ignored.
    history.record(1000, 210, true);
    history.record(2000, 250, true);
    auto point = history.get(0);
    require(point.count == 3 && point.sum == 6900 && point.minimum == 2100 && point.maximum == 2500);
    history.record(3000, 0, false);
    history.record(4000, std::numeric_limits<double>::quiet_NaN(), true);
    history.record(5000, -1, true);
    history.record(6000, 999.95, true);
    history.record(7000, std::numeric_limits<double>::infinity(), true);
    require(history.get(0).count == 3);
    history.record(300000, 0, true);
    require(history.get(1).count == 1 && history.get(1).minimum == 0);
    history.record(299000, 300, true); // Backward timestamps cannot corrupt old buckets.
    require(history.get(0).count == 3);
    history.record(900000, 240.14, true);
    require(history.get(2).count == 0 && history.get(3).sum == 2401);
    const auto wrap = static_cast<std::uint64_t>(bth::VoltageHistory::capacity) * 300000;
    history.record(wrap, 220, true);
    require(history.get(0).count == 0 && history.get(1).count == 1);
    require(history.get(static_cast<std::uint32_t>(bth::VoltageHistory::capacity)).sum == 2200);
    history.record(wrap * 3, 220, false);
    require(history.get(static_cast<std::uint32_t>(bth::VoltageHistory::capacity)).count == 0);
    bth::VoltageHistory full;
    for (std::uint64_t second = 0; second < 300; ++second) full.record(second * 1000, 999.9, true);
    require(full.get(0).count == 300 && full.get(0).sum == 2999700);
    full.record(1ULL << 32, 230, true);
    require(full.get(static_cast<std::uint32_t>((1ULL << 32) / 300000)).count == 1);
}
