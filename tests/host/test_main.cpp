#include "bth/core/runtime.hpp"
#include "bth/heartbeat/heartbeat.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using bth::Phase;
using bth::Runtime;
using bth::RuntimeState;
using bth::Status;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; \
        std::exit(EXIT_FAILURE); \
    } \
} while (false)

struct FakeModule final : bth::Module {
    const char* name;
    std::vector<std::string>& events;
    bth::Dependencies needs{};
    Phase fail_at = Phase::none;

    FakeModule(const char* id, std::vector<std::string>& log) : name(id), events(log) {}
    const char* id() const noexcept override { return name; }
    bth::Dependencies dependencies() const noexcept override { return needs; }
    Status record(Phase phase, const char* event) noexcept {
        events.emplace_back(std::string(name) + '.' + event);
        return phase == fail_at ? Status::hardware_error : Status::ok;
    }
    Status init() noexcept override { return record(Phase::init, "init"); }
    Status start() noexcept override { return record(Phase::start, "start"); }
    Status tick(std::uint64_t) noexcept override { return record(Phase::tick, "tick"); }
    void stop() noexcept override { events.emplace_back(std::string(name) + ".stop"); }
    void deinit() noexcept override { events.emplace_back(std::string(name) + ".deinit"); }
};

void successful_lifecycle() {
    std::vector<std::string> events;
    FakeModule a("a", events), b("b", events);
    const char* deps[] = {"a"};
    b.needs = {deps, 1};
    Runtime runtime;
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.add(b) == Status::ok);
    CHECK(runtime.start() == Status::ok);
    CHECK(runtime.state() == RuntimeState::running);
    CHECK(runtime.tick(100) == Status::ok);
    runtime.stop();
    const std::vector<std::string> expected = {
        "a.init", "b.init", "a.start", "b.start", "a.tick", "b.tick",
        "b.stop", "a.stop", "b.deinit", "a.deinit"};
    CHECK(events == expected);
    runtime.stop();
    CHECK(events == expected);
    CHECK(runtime.state() == RuntimeState::stopped);
}

void init_failure_cleans_partial_module() {
    std::vector<std::string> events;
    FakeModule a("a", events), b("b", events), c("c", events);
    b.fail_at = Phase::init;
    Runtime runtime;
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.add(b) == Status::ok);
    CHECK(runtime.add(c) == Status::ok);
    CHECK(runtime.start() == Status::hardware_error);
    CHECK(events == (std::vector<std::string>{"a.init", "b.init", "b.deinit", "a.deinit"}));
    CHECK(runtime.state() == RuntimeState::failed);
    CHECK(runtime.failure().phase == Phase::init);
    CHECK(std::string(runtime.failure().module) == "b");
    runtime.stop();
    CHECK(runtime.failure().status == Status::hardware_error);
    b.fail_at = Phase::none;
    CHECK(runtime.start() == Status::ok);
    CHECK(runtime.failure().status == Status::ok);
}

void start_failure_stops_partial_module() {
    std::vector<std::string> events;
    FakeModule a("a", events), b("b", events), c("c", events);
    b.fail_at = Phase::start;
    Runtime runtime;
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.add(b) == Status::ok);
    CHECK(runtime.add(c) == Status::ok);
    CHECK(runtime.start() == Status::hardware_error);
    CHECK(events == (std::vector<std::string>{
        "a.init", "b.init", "c.init", "a.start", "b.start", "b.stop", "a.stop",
        "c.deinit", "b.deinit", "a.deinit"}));
    CHECK(runtime.failure().phase == Phase::start);
    CHECK(runtime.state() == RuntimeState::failed);
}

void tick_failure_stops_all_and_skips_following_ticks() {
    std::vector<std::string> events;
    FakeModule a("a", events), b("b", events), c("c", events);
    b.fail_at = Phase::tick;
    Runtime runtime;
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.add(b) == Status::ok);
    CHECK(runtime.add(c) == Status::ok);
    CHECK(runtime.start() == Status::ok);
    events.clear();
    CHECK(runtime.tick(10) == Status::hardware_error);
    CHECK(events == (std::vector<std::string>{
        "a.tick", "b.tick", "c.stop", "b.stop", "a.stop", "c.deinit", "b.deinit", "a.deinit"}));
    CHECK(runtime.failure().phase == Phase::tick);
    CHECK(runtime.state() == RuntimeState::failed);
}

void missing_dependency_has_no_side_effects() {
    std::vector<std::string> events;
    FakeModule module("consumer", events);
    const char* deps[] = {"missing"};
    module.needs = {deps, 1};
    Runtime runtime;
    CHECK(runtime.add(module) == Status::ok);
    CHECK(runtime.start() == Status::missing_dependency);
    CHECK(events.empty());
    CHECK(runtime.failure().phase == Phase::validation);
    CHECK(std::string(runtime.failure().dependency) == "missing");
}

void reversed_and_cyclic_dependencies_are_rejected() {
    for (bool cycle : {false, true}) {
        std::vector<std::string> events;
        FakeModule a("a", events), b("b", events);
        const char* a_deps[] = {"b"};
        const char* b_deps[] = {"a"};
        a.needs = {a_deps, 1};
        if (cycle) { b.needs = {b_deps, 1}; }
        Runtime runtime;
        CHECK(runtime.add(a) == Status::ok);
        CHECK(runtime.add(b) == Status::ok);
        CHECK(runtime.start() == Status::dependency_order);
        CHECK(events.empty());
    }
}

void self_dependency_is_rejected() {
    std::vector<std::string> events;
    FakeModule module("self", events);
    const char* deps[] = {"self"};
    module.needs = {deps, 1};
    Runtime runtime;
    CHECK(runtime.add(module) == Status::ok);
    CHECK(runtime.start() == Status::dependency_order);
    CHECK(events.empty());
}

void invalid_dependency_data_is_rejected() {
    const char* empty[] = {""};
    const char* null[] = {nullptr};
    for (const auto deps : {bth::Dependencies{nullptr, 1}, bth::Dependencies{empty, 1},
                            bth::Dependencies{null, 1}}) {
        std::vector<std::string> events;
        FakeModule module("module", events);
        module.needs = deps;
        Runtime runtime;
        CHECK(runtime.add(module) == Status::ok);
        CHECK(runtime.start() == Status::invalid_argument);
        CHECK(events.empty());
    }
}

void registration_rejects_bad_ids_and_duplicates() {
    std::vector<std::string> events;
    FakeModule a("a", events), duplicate("a", events), empty("", events), null(nullptr, events);
    Runtime runtime;
    CHECK(runtime.add(empty) == Status::invalid_argument);
    CHECK(runtime.add(null) == Status::invalid_argument);
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.add(a) == Status::duplicate_module);
    CHECK(runtime.add(duplicate) == Status::duplicate_module);
    CHECK(runtime.size() == 1);
}

void capacity_is_bounded() {
    std::vector<std::string> events;
    std::array<std::string, Runtime::max_modules + 1> names;
    std::vector<FakeModule> modules;
    modules.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        names[i] = "module_" + std::to_string(i);
        modules.emplace_back(names[i].c_str(), events);
    }
    Runtime runtime;
    for (std::size_t i = 0; i < Runtime::max_modules; ++i) {
        CHECK(runtime.add(modules[i]) == Status::ok);
    }
    CHECK(runtime.add(modules.back()) == Status::capacity_exceeded);
    CHECK(runtime.size() == Runtime::max_modules);
}

void invalid_transitions_do_not_restart_modules() {
    std::vector<std::string> events;
    FakeModule a("a", events), b("b", events);
    Runtime runtime;
    CHECK(runtime.tick(0) == Status::invalid_state);
    CHECK(runtime.add(a) == Status::ok);
    CHECK(runtime.start() == Status::ok);
    const auto started_events = events;
    CHECK(runtime.start() == Status::invalid_state);
    CHECK(runtime.add(b) == Status::invalid_state);
    CHECK(events == started_events);
    runtime.stop();
    CHECK(runtime.add(b) == Status::invalid_state);
    CHECK(runtime.tick(0) == Status::invalid_state);
}

void destructor_stops_running_modules() {
    std::vector<std::string> events;
    FakeModule module("a", events);
    {
        Runtime runtime;
        CHECK(runtime.add(module) == Status::ok);
        CHECK(runtime.start() == Status::ok);
    }
    CHECK(events == (std::vector<std::string>{"a.init", "a.start", "a.stop", "a.deinit"}));
}

void empty_runtime_can_run() {
    Runtime runtime;
    CHECK(runtime.start() == Status::ok);
    CHECK(runtime.tick(0) == Status::ok);
    runtime.stop();
}

struct FakeSink final : bth::HeartbeatSink {
    std::vector<std::uint64_t> reports;
    void report(std::uint64_t now_ms) noexcept override { reports.push_back(now_ms); }
};

void heartbeat_obeys_interval_and_restarts() {
    FakeSink sink;
    bth::Heartbeat heartbeat(sink, 1000);
    Runtime runtime;
    CHECK(runtime.add(heartbeat) == Status::ok);
    CHECK(runtime.start() == Status::ok);
    for (auto time : {100ULL, 1099ULL, 1100ULL, 9000ULL, 9001ULL}) {
        CHECK(runtime.tick(time) == Status::ok);
    }
    CHECK(sink.reports == (std::vector<std::uint64_t>{100, 1100, 9000}));
    runtime.stop();
    CHECK(runtime.start() == Status::ok);
    CHECK(runtime.tick(9002) == Status::ok);
    CHECK(sink.reports.back() == 9002);
}

void heartbeat_supports_uptime_beyond_32_bits() {
    FakeSink sink;
    bth::Heartbeat heartbeat(sink, 1000);
    Runtime runtime;
    CHECK(runtime.add(heartbeat) == Status::ok);
    CHECK(runtime.start() == Status::ok);
    constexpr std::uint64_t start = 0xFFFFFFFFULL - 100;
    CHECK(runtime.tick(start) == Status::ok);
    CHECK(runtime.tick(start + 999) == Status::ok);
    CHECK(runtime.tick(start + 1000) == Status::ok);
    CHECK(sink.reports == (std::vector<std::uint64_t>{start, start + 1000}));
}

void heartbeat_rejects_invalid_configuration_and_state() {
    FakeSink sink;
    bth::Heartbeat heartbeat(sink, 0);
    CHECK(heartbeat.start() == Status::invalid_state);
    CHECK(heartbeat.tick(0) == Status::invalid_state);
    Runtime runtime;
    CHECK(runtime.add(heartbeat) == Status::ok);
    CHECK(runtime.start() == Status::invalid_argument);
    CHECK(runtime.failure().phase == Phase::init);
    CHECK(sink.reports.empty());
}

int main() {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"successful lifecycle", successful_lifecycle},
        {"init rollback", init_failure_cleans_partial_module},
        {"start rollback", start_failure_stops_partial_module},
        {"tick rollback", tick_failure_stops_all_and_skips_following_ticks},
        {"missing dependency", missing_dependency_has_no_side_effects},
        {"order and cycles", reversed_and_cyclic_dependencies_are_rejected},
        {"self dependency", self_dependency_is_rejected},
        {"invalid dependency data", invalid_dependency_data_is_rejected},
        {"registration validation", registration_rejects_bad_ids_and_duplicates},
        {"capacity", capacity_is_bounded},
        {"invalid transitions", invalid_transitions_do_not_restart_modules},
        {"destructor cleanup", destructor_stops_running_modules},
        {"empty runtime", empty_runtime_can_run},
        {"heartbeat scheduling", heartbeat_obeys_interval_and_restarts},
        {"64-bit uptime", heartbeat_supports_uptime_beyond_32_bits},
        {"heartbeat validation", heartbeat_rejects_invalid_configuration_and_state},
    };
    for (const auto& test : tests) {
        test.run();
        std::cout << "PASS: " << test.name << '\n';
    }
    std::cout << "Passed " << sizeof(tests) / sizeof(tests[0]) << " tests\n";
}
