#include "bth/web/voltage.hpp"
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
}
