#include "bth/dc/dc.hpp"
#include "bth/core/runtime.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
namespace {
#define CHECK_DC(c) do { if (!(c)) { std::cerr << "dc:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct Input final : bth::DcInput {
    double mv = 1000;
    unsigned count = 40;
    bool invalid = false;
    Status result = Status::ok;
    Status read_dc(bth::DcBatch& batch) noexcept override {
        batch = {mv * count, count, invalid};
        return result;
    }
};
struct Sink final : bth::DcSink {
    std::vector<bth::DcReading> readings;
    void report(const bth::DcReading& r) noexcept override { readings.push_back(r); }
};
void window(bth::Runtime& runtime, std::uint64_t base) {
    for (unsigned ms = 20; ms <= 1000; ms += 20) CHECK_DC(runtime.tick(base + ms) == Status::ok);
}
}
void test_dc() {
    Input input;
    Sink sink;
    bth::DcModule dc(input, sink, 13);
    CHECK_DC(dc.start() == Status::invalid_state && dc.tick(0) == Status::invalid_state);
    dc.stop(); dc.deinit();
    bth::Runtime runtime;
    CHECK_DC(runtime.add(dc) == Status::ok && runtime.start() == Status::ok);
    CHECK_DC(dc.init() == Status::invalid_state && dc.start() == Status::invalid_state);
    const std::uint64_t base = 1ULL << 32;
    CHECK_DC(runtime.tick(base) == Status::ok);
    window(runtime, base);
    CHECK_DC(sink.readings.size() == 1 && sink.readings.back().valid);
    CHECK_DC(std::abs(sink.readings.back().volts - 13) < 0.00001);
    input.mv = 3000.0 / 13;
    window(runtime, base + 1000);
    CHECK_DC(sink.readings.back().valid && std::abs(sink.readings.back().volts - 3) < 0.00001);
    input.mv = 29800.0 / 13;
    window(runtime, base + 2000);
    CHECK_DC(sink.readings.back().valid && std::abs(sink.readings.back().volts - 29.8) < 0.00001);
    // Integer calibrated mV may round slightly above 30 V before display rounding.
    input.mv = 2308;
    runtime.stop();
    CHECK_DC(runtime.start() == Status::ok && runtime.tick(base + 2000) == Status::ok);
    window(runtime, base + 2000);
    CHECK_DC(sink.readings.back().valid && std::abs(sink.readings.back().volts - 30.004) < 0.00001);
    input.mv = 2400;
    window(runtime, base + 3000);
    CHECK_DC(!sink.readings.back().valid);
    input.mv = 1000; input.invalid = true;
    window(runtime, base + 4000);
    CHECK_DC(!sink.readings.back().valid);
    input.invalid = false;
    window(runtime, base + 5000);
    CHECK_DC(sink.readings.back().valid);
    input.count = 0;
    window(runtime, base + 6000);
    CHECK_DC(!sink.readings.back().valid && sink.readings.back().samples == 0);
    input.count = 40;
    CHECK_DC(runtime.tick(base + 10000) == Status::ok && !sink.readings.back().valid);
    input.mv = std::numeric_limits<double>::quiet_NaN();
    window(runtime, base + 10000);
    CHECK_DC(!sink.readings.back().valid);
    input.mv = 1000; input.count = 5000;
    window(runtime, base + 11000);
    CHECK_DC(!sink.readings.back().valid);
    input.count = 40; input.result = Status::hardware_error;
    CHECK_DC(runtime.tick(base + 12020) == Status::hardware_error);
    CHECK_DC(dc.tick(0) == Status::invalid_state);
    runtime.stop(); input.result = Status::ok;
    CHECK_DC(runtime.start() == Status::ok && runtime.tick(0) == Status::ok);
    window(runtime, 0);
    CHECK_DC(sink.readings.back().valid);
    runtime.stop(); runtime.stop(); dc.deinit();
    for (double ratio : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        bth::DcModule bad(input, sink, ratio);
        CHECK_DC(bad.init() == Status::invalid_argument);
        bad.deinit();
    }
    input.result = Status::hardware_error;
    bth::DcModule failed(input, sink, 13);
    bth::Runtime failure_runtime;
    CHECK_DC(failure_runtime.add(failed) == Status::ok);
    CHECK_DC(failure_runtime.start() == Status::hardware_error);
    failure_runtime.stop();
    input.result = Status::ok;
    CHECK_DC(failure_runtime.start() == Status::ok);
    static constexpr const char* provider[] = {"mains"};
    bth::DcModule dependent(input, sink, 13, {provider, 1});
    bth::Runtime missing;
    CHECK_DC(missing.add(dependent) == Status::ok && missing.start() == Status::missing_dependency);
}
