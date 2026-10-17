#include "bth/buzzer/buzzer.hpp"
#include "bth/core/runtime.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
#define CHECK_BUZZER(c) do { if (!(c)) { std::cerr << "buzzer:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct Output final : bth::BuzzerOutput {
    bool acquired = false, on = false;
    unsigned pulses = 0, releases = 0;
    Status init_result = Status::ok, set_result = Status::ok;
    Status init() noexcept override { acquired = true; return init_result; }
    Status set(bool value) noexcept override {
        CHECK_BUZZER(acquired);
        on = value;  // An operation may fail after changing the output.
        if (value) ++pulses;
        return set_result;
    }
    void deinit() noexcept override { acquired = on = false; ++releases; }
};
struct Provider final : bth::Module {
    const char* id() const noexcept override { return "mains"; }
    Status init() noexcept override { return Status::ok; }
    Status start() noexcept override { return Status::ok; }
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override {}
    void deinit() noexcept override {}
};
bth::MainsReading reading(double volts, bool valid = true, bool calibrated = true) {
    bth::MainsReading r;
    r.volts = volts;
    r.valid = valid;
    r.calibrated = calibrated;
    bth::MainsHistoryTracker history;
    r.history = history.update(volts, valid && calibrated, 0);
    return r;
}
}

void test_buzzer() {
    Output output;
    Provider mains;
    bth::BuzzerModule buzzer(output);
    bth::Runtime runtime;
    CHECK_BUZZER(buzzer.start() == Status::invalid_state);
    CHECK_BUZZER(buzzer.tick(0) == Status::invalid_state);
    buzzer.stop(); buzzer.deinit();
    CHECK_BUZZER(output.releases == 0);
    CHECK_BUZZER(runtime.add(mains) == Status::ok && runtime.add(buzzer) == Status::ok);
    CHECK_BUZZER(runtime.start() == Status::ok);
    CHECK_BUZZER(buzzer.init() == Status::invalid_state && buzzer.start() == Status::invalid_state);
    const std::uint64_t base = (1ULL << 32) + 100;
    CHECK_BUZZER(runtime.tick(base) == Status::ok && !output.on);
    buzzer.report(reading(100));
    CHECK_BUZZER(runtime.tick(base + 1000) == Status::ok && !output.on);
    buzzer.report(reading(99.9));
    const auto outage = base + 2000;
    CHECK_BUZZER(runtime.tick(outage) == Status::ok && output.on && output.pulses == 1);
    CHECK_BUZZER(runtime.tick(outage + 199) == Status::ok && output.on);
    CHECK_BUZZER(runtime.tick(outage + 200) == Status::ok && !output.on);
    for (unsigned ms = 1000; ms < 20000; ms += 1000) {
        buzzer.report(reading(0));
        CHECK_BUZZER(runtime.tick(outage + ms) == Status::ok && !output.on);
    }
    CHECK_BUZZER(runtime.tick(outage + 19999) == Status::ok && output.pulses == 1);
    buzzer.report(reading(0));
    CHECK_BUZZER(runtime.tick(outage + 20000) == Status::ok && output.on && output.pulses == 2);
    // Restoration silences even an unfinished pulse.
    buzzer.report(reading(237));
    CHECK_BUZZER(runtime.tick(outage + 20020) == Status::ok && !output.on);
    buzzer.report(reading(0));
    CHECK_BUZZER(runtime.tick(outage + 21000) == Status::ok && output.on && output.pulses == 3);
    buzzer.report(reading(0, false));
    CHECK_BUZZER(runtime.tick(outage + 21020) == Status::ok && !output.on);
    buzzer.report(reading(0, true, false));
    CHECK_BUZZER(runtime.tick(outage + 22000) == Status::ok && !output.on);
    buzzer.report(reading(std::numeric_limits<double>::quiet_NaN()));
    CHECK_BUZZER(runtime.tick(outage + 23000) == Status::ok && !output.on);
    buzzer.report(reading(0));
    CHECK_BUZZER(runtime.tick(outage + 24000) == Status::ok && output.on);
    // A scheduling stall must not leave a stale outage active or replay missed beeps.
    CHECK_BUZZER(runtime.tick(outage + 26500) == Status::ok && !output.on);
    const auto pulses = output.pulses;
    CHECK_BUZZER(runtime.tick(outage + 100000) == Status::ok && output.pulses == pulses);
    buzzer.report(reading(0));
    CHECK_BUZZER(runtime.tick(outage + 101000) == Status::ok && output.on);
    runtime.stop();
    CHECK_BUZZER(!output.on && !output.acquired && output.releases == 1);
    runtime.stop(); buzzer.deinit();
    CHECK_BUZZER(output.releases == 1);
    buzzer.report(reading(0));  // Reports while stopped cannot trigger a restart pulse.
    CHECK_BUZZER(runtime.start() == Status::ok && runtime.tick(0) == Status::ok && !output.on);
    runtime.stop();

    // Partial init, failed start and failed activation all roll back to silence.
    for (unsigned failure = 0; failure < 3; ++failure) {
        Output failed_output;
        Provider provider;
        bth::BuzzerModule failed(failed_output);
        bth::Runtime failed_runtime;
        CHECK_BUZZER(failed_runtime.add(provider) == Status::ok && failed_runtime.add(failed) == Status::ok);
        if (failure == 0) failed_output.init_result = Status::hardware_error;
        if (failure == 1) failed_output.set_result = Status::hardware_error;
        if (failure < 2) {
            CHECK_BUZZER(failed_runtime.start() == Status::hardware_error);
        } else {
            CHECK_BUZZER(failed_runtime.start() == Status::ok);
            failed_output.set_result = Status::hardware_error;
            failed.report(reading(0));
            CHECK_BUZZER(failed_runtime.tick(0) == Status::hardware_error);
        }
        CHECK_BUZZER(!failed_output.on && !failed_output.acquired && failed_output.releases == 1);
        failed_runtime.stop();
        failed_output.init_result = failed_output.set_result = Status::ok;
        CHECK_BUZZER(failed_runtime.start() == Status::ok);
        failed.report(reading(0));
        CHECK_BUZZER(failed_runtime.tick(0) == Status::ok && failed_output.on);
        failed_runtime.stop();
    }
    Output unbound_output;
    {
        Output adjustable_output;
        Provider provider;
        bth::BuzzerModule adjustable(adjustable_output);
        bth::Runtime adjustable_runtime;
        CHECK_BUZZER(adjustable_runtime.add(provider) == Status::ok);
        CHECK_BUZZER(adjustable_runtime.add(adjustable) == Status::ok);
        CHECK_BUZZER(adjustable.set_interval_seconds(0) == Status::ok);
        CHECK_BUZZER(adjustable_runtime.start() == Status::ok);
        adjustable.report(reading(0));
        CHECK_BUZZER(adjustable_runtime.tick(0) == Status::ok && adjustable_output.pulses == 0);
        CHECK_BUZZER(adjustable.set_interval_seconds(3) == Status::ok);
        CHECK_BUZZER(adjustable_runtime.tick(20) == Status::ok && adjustable_output.on);
        CHECK_BUZZER(adjustable_runtime.tick(220) == Status::ok && !adjustable_output.on);
        adjustable.report(reading(0));
        CHECK_BUZZER(adjustable_runtime.tick(2000) == Status::ok);
        CHECK_BUZZER(adjustable_runtime.tick(3019) == Status::ok && !adjustable_output.on);
        CHECK_BUZZER(adjustable_runtime.tick(3020) == Status::ok && adjustable_output.pulses == 2);
        CHECK_BUZZER(adjustable.set_interval_seconds(0) == Status::ok);
        CHECK_BUZZER(adjustable_runtime.tick(3040) == Status::ok && !adjustable_output.on);
        for (unsigned ms = 4000; ms < 30000; ms += 1000) {
            adjustable.report(reading(0));
            CHECK_BUZZER(adjustable_runtime.tick(ms) == Status::ok && adjustable_output.pulses == 2);
        }
        CHECK_BUZZER(adjustable.set_interval_seconds(86401) == Status::invalid_argument);
        CHECK_BUZZER(adjustable_runtime.tick(30000) == Status::ok && !adjustable_output.on);
        CHECK_BUZZER(adjustable.set_interval_seconds(86400) == Status::ok);
        adjustable.report(reading(0));
        CHECK_BUZZER(adjustable_runtime.tick(31000) == Status::ok && adjustable_output.pulses == 3);
        CHECK_BUZZER(adjustable_runtime.tick(31200) == Status::ok && !adjustable_output.on);
        adjustable.report(reading(0));
        CHECK_BUZZER(adjustable_runtime.tick(86430999) == Status::ok && adjustable_output.pulses == 3);
        CHECK_BUZZER(adjustable_runtime.tick(86431000) == Status::ok && adjustable_output.pulses == 4);
    }
    bth::BuzzerModule unbound(unbound_output);
    bth::Runtime unbound_runtime;
    CHECK_BUZZER(unbound_runtime.add(unbound) == Status::ok);
    CHECK_BUZZER(unbound_runtime.start() == Status::missing_dependency && !unbound_output.acquired);
}
