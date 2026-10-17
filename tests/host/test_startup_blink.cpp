#include "bth/startup_blink/startup_blink.hpp"
#include "bth/core/runtime.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
#define CHECK_BLINK(condition) do { if (!(condition)) {     std::cerr << "startup_blink:" << __LINE__ << ": " #condition "\n";     std::exit(EXIT_FAILURE); } } while (false)

struct FakeLed final : bth::LedOutput {
    bool acquired = false;
    bool on = false;
    bool fail_init = false;
    bool fail_set = false;
    int releases = 0;
    std::vector<bool> edges;
    bth::Status init() noexcept override {
        acquired = true;
        return fail_init ? bth::Status::hardware_error : bth::Status::ok;
    }
    bth::Status set(bool value) noexcept override {
        CHECK_BLINK(acquired);
        on = value;  // A failed driver call can have partially changed hardware.
        if (fail_set) { return bth::Status::hardware_error; }
        edges.push_back(value);
        return bth::Status::ok;
    }
    void deinit() noexcept override {
        acquired = false;
        on = false;
        ++releases;
    }
};
}

void test_startup_blink() {
    using bth::Status;
    // Runtime mode changes interrupt an old group, pause, then show only the new digit.
    {
        FakeLed led;
        bth::StartupBlinkModule blink(led, {2, 200, 200, true, 2000, 50, 2000});
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok && runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(0) == Status::ok && led.on);
        blink.set_count(3);
        CHECK_BLINK(runtime.tick(50) == Status::ok && !led.on);
        CHECK_BLINK(runtime.tick(2000) == Status::ok && !led.on);
        std::uint64_t now = 4000;
        for (auto count : {3U, 1U, 2U, 3U, 8U}) {
            blink.set_count(count);
            if (count == 8) { CHECK_BLINK(runtime.tick(now) == Status::ok && !led.on); now += 2000; }
            const auto edges = led.edges.size();
            for (unsigned pulse = 0; pulse < count; ++pulse) {
                blink.set_count(count); // Repeated status updates must not restart a group.
                CHECK_BLINK(runtime.tick(now) == Status::ok && led.on);
                now += 200;
                CHECK_BLINK(runtime.tick(now) == Status::ok && !led.on);
                if (pulse + 1 < count) now += 200;
            }
            CHECK_BLINK(led.edges.size() == edges + count * 2);
            now += 2000;
            CHECK_BLINK(runtime.tick(now) == Status::ok && led.on);
            const auto next = count == 3 ? 1U : count + 1;
            blink.set_count(next);
            now += 20;
            CHECK_BLINK(runtime.tick(now) == Status::ok && !led.on);
            CHECK_BLINK(runtime.tick(now + 1999) == Status::ok && !led.on);
            now += 2000;
        }
        blink.set_count(0);
        CHECK_BLINK(runtime.tick(now) == Status::ok && !led.on);
        CHECK_BLINK(runtime.tick(now + 10000) == Status::ok && !led.on);
        blink.set_count(2);
        CHECK_BLINK(runtime.tick(now + 10020) == Status::ok && !led.on);
        CHECK_BLINK(runtime.tick(now + 12020) == Status::ok && led.on);
        led.fail_set = true;
        blink.set_count(1);
        CHECK_BLINK(runtime.tick(now + 12040) == Status::hardware_error);
        CHECK_BLINK(!led.acquired && !led.on);
    }
    for (bool pulse_on : {true, false}) {
        FakeLed led;
        bth::StartupBlinkConfig config;
        config.pulse_on = pulse_on;
        bth::StartupBlinkModule blink(led, config);
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        constexpr std::uint64_t base = 0xFFFFFFFFULL - 100;
        for (std::uint64_t edge = 0; edge < 40; ++edge) {
            CHECK_BLINK(runtime.tick(base + edge * 50) == Status::ok);
            CHECK_BLINK(led.on == (edge % 2 == 0));
            const auto size = led.edges.size();
            CHECK_BLINK(runtime.tick(base + edge * 50 + 49) == Status::ok);
            CHECK_BLINK(led.edges.size() == size);
        }
        CHECK_BLINK(led.edges.size() == 40);
        CHECK_BLINK(runtime.tick(base + 2000) == Status::ok);
        CHECK_BLINK(led.on == !pulse_on);
        CHECK_BLINK(runtime.tick(base + 3999) == Status::ok);
        CHECK_BLINK(led.on == !pulse_on);
        for (std::uint64_t group = 0; group < 2; ++group) {
            const auto start = base + 4000 + group * 3800;
            const auto size = led.edges.size();
            for (std::uint64_t edge = 0; edge < 10; ++edge) {
                CHECK_BLINK(runtime.tick(start + edge * 200) == Status::ok);
                CHECK_BLINK(led.on == (edge % 2 == 0 ? pulse_on : !pulse_on));
                CHECK_BLINK(runtime.tick(start + edge * 200 + 199) == Status::ok);
                CHECK_BLINK(led.edges.size() == size + edge + 1);
            }
            CHECK_BLINK(runtime.tick(start + 3799) == Status::ok);
            CHECK_BLINK(led.on == !pulse_on && led.edges.size() == size + 10);
        }
        runtime.stop();
        CHECK_BLINK(!led.on && led.releases == 1);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(base + 20000) == Status::ok && led.on);
        CHECK_BLINK(runtime.tick(base + 20050) == Status::ok && !led.on);
    }
    {
        FakeLed led;
        bth::StartupBlinkModule blink(led);
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(0) == Status::ok && led.on);
        CHECK_BLINK(runtime.tick(5000) == Status::ok && !led.on);
        CHECK_BLINK(led.edges.size() == 2);
        CHECK_BLINK(runtime.tick(6999) == Status::ok && !led.on);
        CHECK_BLINK(runtime.tick(7000) == Status::ok && led.on);
        // A late digit tick must preserve the remaining visible pulse count.
        CHECK_BLINK(runtime.tick(20000) == Status::ok && !led.on);
        CHECK_BLINK(runtime.tick(20200) == Status::ok && led.on);
    }
    for (bool pulse_on : {true, false}) {
        FakeLed led;
        bth::StartupBlinkConfig config;
        config.count = 0;
        config.pulse_on = pulse_on;
        bth::StartupBlinkModule blink(led, config);
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(0) == Status::ok && led.on);
        CHECK_BLINK(runtime.tick(2000) == Status::ok && led.on == !pulse_on);
        const auto size = led.edges.size();
        CHECK_BLINK(runtime.tick(100000) == Status::ok);
        CHECK_BLINK(led.edges.size() == size && led.on == !pulse_on);
    }
    // Failures in the burst, transition to idle, and digit edges all roll back.
    for (auto failure_ms : {50U, 2000U, 4000U, 4200U}) {
        FakeLed led;
        bth::StartupBlinkModule blink(led);
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(0) == Status::ok);
        if (failure_ms >= 4000) { CHECK_BLINK(runtime.tick(2000) == Status::ok); }
        if (failure_ms >= 4200) { CHECK_BLINK(runtime.tick(4000) == Status::ok); }
        led.fail_set = true;
        CHECK_BLINK(runtime.tick(failure_ms) == Status::hardware_error);
        CHECK_BLINK(runtime.state() == bth::RuntimeState::failed);
        CHECK_BLINK(!led.acquired && !led.on && led.releases == 1);
    }

    {
        FakeLed led;
        bth::StartupBlinkModule blink(led, {5, 200, 200, true, 0, 50, 0});
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(!led.on);
        constexpr std::uint64_t base = 0xFFFFFFFFULL - 100;
        for (std::uint64_t edge = 0; edge < 10; ++edge) {
            CHECK_BLINK(runtime.tick(base + edge * 200) == Status::ok);
            CHECK_BLINK(led.edges.size() == edge + 1);
            CHECK_BLINK(led.on == (edge % 2 == 0));
            CHECK_BLINK(runtime.tick(base + edge * 200 + 199) == Status::ok);
            CHECK_BLINK(led.edges.size() == edge + 1);
        }
        CHECK_BLINK(runtime.tick(base + 100000) == Status::ok);
        CHECK_BLINK(led.edges.size() == 10 && !led.on);
        runtime.stop();
        const auto edges = led.edges.size();
        runtime.stop();
        CHECK_BLINK(led.releases == 1 && led.edges.size() == edges);
        CHECK_BLINK(runtime.start() == Status::ok);
        CHECK_BLINK(runtime.tick(base + 200000) == Status::ok && led.on);
        runtime.stop();
        CHECK_BLINK(!led.on && !led.acquired && led.releases == 2);
    }
    for (auto count : {0U, 1U, 8U}) {
        FakeLed led;
        bth::StartupBlinkModule blink(led, {count, 100, 300, true, 0, 50, 0});
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::ok);
        // Delayed ticks still produce separate, visible pulses, never a catch-up burst.
        for (std::uint64_t tick = 0; tick < 30; ++tick) {
            CHECK_BLINK(runtime.tick(tick * 1000) == Status::ok);
        }
        CHECK_BLINK(led.edges.size() == count * 2 && !led.on);
    }
    for (bool fail_init : {false, true}) {
        FakeLed led;
        led.fail_init = fail_init;
        bth::StartupBlinkModule blink(led, {5, 200, 200, true, 0, 50, 0});
        bth::Runtime runtime;
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        if (fail_init) {
            CHECK_BLINK(runtime.start() == Status::hardware_error);
        } else {
            CHECK_BLINK(runtime.start() == Status::ok);
            led.fail_set = true;
            CHECK_BLINK(runtime.tick(0) == Status::hardware_error);
        }
        CHECK_BLINK(runtime.state() == bth::RuntimeState::failed);
        CHECK_BLINK(!led.acquired && !led.on && led.releases == 1);
        runtime.stop();
        CHECK_BLINK(led.releases == 1);
    }
    for (const auto config : {bth::StartupBlinkConfig{5, 0, 200},
                               bth::StartupBlinkConfig{5, 200, 0},
                               bth::StartupBlinkConfig{5, 200, 200, true, 1000, 0}}) {
        FakeLed led;
        bth::StartupBlinkModule blink(led, config);
        bth::Runtime runtime;
        CHECK_BLINK(blink.start() == Status::invalid_state);
        CHECK_BLINK(blink.tick(0) == Status::invalid_state);
        CHECK_BLINK(runtime.add(blink) == Status::ok);
        CHECK_BLINK(runtime.start() == Status::invalid_argument);
        CHECK_BLINK(!led.acquired && led.releases == 0);
    }
}
