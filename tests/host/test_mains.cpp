#include "bth/mains/mains.hpp"
#include "bth/core/runtime.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
#define CHECK_M(c) do { if (!(c)) { std::cerr << "mains:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct Input final : bth::MainsInput {
    int fail = 0, releases = 0, stops = 0, reads = 0;
    unsigned sample = 0;
    double amplitude = 400, offset = 1800;
    bool loss = false, empty = false, acquired = false, running = false;
    Status init() noexcept override { acquired = true; return fail == 1 ? Status::hardware_error : Status::ok; }
    Status start() noexcept override { running = true; return fail == 2 ? Status::hardware_error : Status::ok; }
    Status read(std::uint16_t* values, std::size_t capacity, std::size_t& count, bool& lost) noexcept override {
        CHECK_M(running && acquired && capacity >= 40);
        ++reads;
        count = empty ? 0 : 40;
        lost = loss;
        for (std::size_t i = 0; i < count; ++i, ++sample)
            values[i] = static_cast<std::uint16_t>(std::lround(offset + amplitude * std::sin(sample * 6.283185307179586 / 40)));
        return fail == 3 ? Status::hardware_error : Status::ok;
    }
    void stop() noexcept override { ++stops; running = false; }
    void deinit() noexcept override { ++releases; acquired = false; }
};
struct Sink final : bth::MainsSink {
    std::vector<bth::MainsReading> reports;
    void report(const bth::MainsReading& r) noexcept override { reports.push_back(r); }
};
void window(bth::Runtime& runtime, std::uint64_t base) {
    for (unsigned t = 20; t <= 1000; t += 20) CHECK_M(runtime.tick(base + t) == Status::ok);
}
}
void test_mains() {
    {
        bth::MainsHistoryTracker tracker;
        const std::uint64_t base = 1ULL << 32;
        auto h = tracker.update(230, false, base);
        CHECK_M(!h.current.known && !h.previous.known);
        h = tracker.update(100, true, base);
        CHECK_M(h.current.known && h.current.present && h.current.duration_ms == 0);
        for (unsigned i = 1; i <= 3600; ++i) h = tracker.update(230, true, base + i*1000ULL);
        CHECK_M(h.current.duration_ms == 3600000 && !h.previous.known);
        h = tracker.update(99.9, true, base + 3601000);
        CHECK_M(!h.current.present && h.current.duration_ms == 0);
        CHECK_M(h.previous.known && h.previous.present && h.previous.duration_ms == 3601000);
        h = tracker.update(0, true, base + 3602000);
        CHECK_M(!h.current.present && h.current.duration_ms == 1000);
        h = tracker.update(100, true, base + 3603000);
        CHECK_M(h.current.present && !h.previous.present && h.previous.duration_ms == 2000);
        h = tracker.update(0, false, base + 3604000);
        CHECK_M(!h.current.known && !h.previous.known);
        h = tracker.update(230, true, base + 3605000);
        CHECK_M(h.current.duration_ms == 0 && !h.previous.known);
        h = tracker.update(0, true, base + 3607500);
        CHECK_M(!h.current.present && !h.previous.known && h.current.duration_ms == 0);
        h = tracker.update(230, true, 0);
        CHECK_M(h.current.present && !h.previous.known && h.current.duration_ms == 0);
        h = tracker.update(std::nan(""), true, 1000);
        CHECK_M(!h.current.known);
    }
    {
        Input input;
        Sink sink;
        bth::MainsModule mains(input, sink, 0.8);
        CHECK_M(mains.start() == Status::invalid_state);
        CHECK_M(mains.tick(0) == Status::invalid_state);
        mains.stop(); mains.deinit();
        CHECK_M(input.releases == 0);
        bth::Runtime runtime;
        CHECK_M(runtime.add(mains) == Status::ok && runtime.start() == Status::ok);
        CHECK_M(mains.init() == Status::invalid_state && mains.start() == Status::invalid_state);
        const std::uint64_t base = (1ULL << 32);
        CHECK_M(runtime.tick(base) == Status::ok);
        CHECK_M(sink.reports.empty());
        window(runtime, base);
        CHECK_M(sink.reports.size() == 1);
        auto r = sink.reports.back();
        CHECK_M(r.valid && r.calibrated && r.samples == 2040);
        CHECK_M(r.history.current.known && r.history.current.present && !r.history.previous.known);
        CHECK_M(std::abs(r.rms - 400 / std::sqrt(2.0)) < 0.5);
        CHECK_M(std::abs(r.mean - 1800) < 0.1 && std::abs(r.volts - r.rms * 0.8) < 0.01);
        input.offset = 2300;
        window(runtime, base + 1000);
        CHECK_M(std::abs(sink.reports.back().rms - r.rms) < 0.01);
        input.amplitude = 0;
        window(runtime, base + 2000);
        CHECK_M(sink.reports.back().valid && sink.reports.back().rms == 0);
        CHECK_M(!sink.reports.back().history.current.present && sink.reports.back().history.previous.duration_ms == 2000);
        input.loss = true;
        window(runtime, base + 3000);
        CHECK_M(!sink.reports.back().valid);
        CHECK_M(!sink.reports.back().history.current.known);
        input.loss = false;
        window(runtime, base + 4000);
        CHECK_M(sink.reports.back().valid);
        input.offset = 4095;
        window(runtime, base + 5000);
        CHECK_M(!sink.reports.back().valid);
        input.empty = true;
        window(runtime, base + 6000);
        CHECK_M(!sink.reports.back().valid && sink.reports.back().samples == 0);
        runtime.stop(); mains.stop(); mains.deinit();
        CHECK_M(input.stops == 1 && input.releases == 1);
        CHECK_M(runtime.start() == Status::ok);
        input.empty = false; input.offset = 1800; input.amplitude = 400;
        CHECK_M(runtime.tick(0) == Status::ok);
        window(runtime, 0);
        CHECK_M(sink.reports.back().valid);
        CHECK_M(runtime.tick(5000) == Status::ok);
        CHECK_M(!sink.reports.back().valid);
    }
    for (int phase = 1; phase <= 3; ++phase) {
        Input input; input.fail = phase;
        Sink sink;
        bth::MainsModule mains(input, sink);
        bth::Runtime runtime;
        CHECK_M(runtime.add(mains) == Status::ok);
        if (phase < 3) CHECK_M(runtime.start() == Status::hardware_error);
        else {
            CHECK_M(runtime.start() == Status::ok);
            CHECK_M(runtime.tick(0) == Status::hardware_error);
        }
        CHECK_M(!input.acquired && !input.running && input.releases == 1);
        CHECK_M(sink.reports.empty());
        input.fail = 0;
        runtime.stop();
        CHECK_M(runtime.start() == Status::ok);
        CHECK_M(runtime.tick(0) == Status::ok);
        window(runtime, 0);
        CHECK_M(sink.reports.back().valid && !sink.reports.back().calibrated);
        CHECK_M(!sink.reports.back().history.current.known);
    }
    {
        Input input; Sink sink;
        bth::MainsModule mains(input, sink, -1);
        CHECK_M(mains.init() == Status::invalid_argument && !input.acquired);
    }
}
