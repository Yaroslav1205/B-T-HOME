#include "bth/relay/relay.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
#define REQUIRE(condition) do { if (!(condition)) {     std::cerr << "relay:" << __LINE__ << ": " #condition "\n";     std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct Disk { bool on = false; unsigned writes = 0; bth::RelayMode mode = bth::RelayMode::manual; };
struct Store final : bth::RelayStore {
    Disk& disk;
    std::vector<std::string>& events;
    bool fail_open = false;
    bool fail_load = false;
    bool fail_save = false;
    bool opened = false;
    unsigned closed = 0;
    Store(Disk& value, std::vector<std::string>& log) : disk(value), events(log) {}
    Status init() noexcept override {
        opened = true; events.emplace_back("open");
        return fail_open ? Status::hardware_error : Status::ok;
    }
    Status load(bool& on) noexcept override {
        events.emplace_back("load"); on = disk.on;
        return fail_load ? Status::hardware_error : Status::ok;
    }
    Status save(bool on) noexcept override {
        events.emplace_back("save");
        if (fail_save) { return Status::hardware_error; }
        disk.on = on; ++disk.writes; return Status::ok;
    }
    Status load_mode(bth::RelayMode& mode) noexcept override { mode = disk.mode; return Status::ok; }
    Status save_mode(bth::RelayMode mode) noexcept override {
        if (fail_save) return Status::hardware_error;
        disk.mode = mode; return Status::ok;
    }
    void deinit() noexcept override { opened = false; ++closed; }
};
struct Output final : bth::RelayOutput {
    std::vector<std::string>& events;
    bool acquired = false;
    bool on = false;
    bool fail_init = false;
    bool fail_set = false;
    unsigned released = 0;
    explicit Output(std::vector<std::string>& log) : events(log) {}
    Status init() noexcept override {
        acquired = true; on = false; events.emplace_back("off_init");
        return fail_init ? Status::hardware_error : Status::ok;
    }
    Status set(bool value) noexcept override {
        REQUIRE(acquired); events.emplace_back(value ? "on" : "off");
        on = value;
        return fail_set ? Status::hardware_error : Status::ok;
    }
    void deinit() noexcept override { on = false; acquired = false; ++released; }
};
}

void test_relay() {
    Disk disk;
    std::vector<std::string> events;
    // A fresh flash starts OFF; acknowledgement follows commit then output update.
    {
        Store store(disk, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.set(true) == Status::invalid_state);
        REQUIRE(relay.init() == Status::ok);
        REQUIRE(relay.start() == Status::ok);
        REQUIRE(!output.on && relay.state().ready);
        events.clear();
        REQUIRE(relay.set(true) == Status::ok);
        REQUIRE(events == (std::vector<std::string>{"save", "on"}));
        REQUIRE(output.on && disk.on && relay.state().saved_on);
        REQUIRE(relay.state().output_known && relay.state().storage_known);
        REQUIRE(disk.writes == 1);
        events.clear();
        REQUIRE(relay.set(true) == Status::ok);
        REQUIRE(events.empty() && disk.writes == 1);
        relay.deinit(); relay.deinit();
        REQUIRE(!output.on && disk.on && store.closed == 1 && output.released == 1);
    }
    // New objects emulate a reboot: restore ON without another flash write.
    {
        Store store(disk, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.init() == Status::ok && !output.on);
        REQUIRE(relay.start() == Status::ok && output.on);
        REQUIRE(disk.writes == 1);
        REQUIRE(relay.set(false) == Status::ok && !disk.on && !output.on);
        relay.deinit();
    }
    {
        Store store(disk, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.init() == Status::ok);
        REQUIRE(relay.start() == Status::ok && !output.on);
        relay.deinit();
    }
    // An uncertain commit never reports success or changes the output.
    {
        Store store(disk, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.init() == Status::ok && relay.start() == Status::ok);
        store.fail_save = true; events.clear();
        REQUIRE(relay.set(true) == Status::hardware_error);
        REQUIRE(events == (std::vector<std::string>{"save"}));
        REQUIRE(!output.on && !relay.state().storage_known && !relay.state().ready);
        REQUIRE(relay.set(false) == Status::invalid_state);
        relay.deinit();
    }
    // A failed GPIO operation leaves durable desired state visible, output unknown.
    {
        Store store(disk, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.init() == Status::ok && relay.start() == Status::ok);
        output.fail_set = true;
        REQUIRE(relay.set(true) == Status::hardware_error);
        REQUIRE(disk.on && relay.state().saved_on && !relay.state().output_known);
        REQUIRE(!relay.state().ready);
        relay.deinit(); REQUIRE(!output.acquired);
    }
    for (int failure = 0; failure < 4; ++failure) {
        Store store(disk, events); Output output(events);
        store.fail_open = failure == 0; store.fail_load = failure == 1;
        output.fail_init = failure == 2; output.fail_set = failure == 3;
        bth::RelayController relay(output, store);
        if (failure == 3) {
            REQUIRE(relay.init() == Status::ok);
            REQUIRE(relay.start() == Status::hardware_error);
        } else { REQUIRE(relay.init() == Status::hardware_error); }
        REQUIRE(!relay.state().ready);
        relay.deinit(); relay.deinit();
        REQUIRE(!store.opened && !output.acquired && store.closed == 1);
        REQUIRE(output.released == (failure >= 2 ? 1U : 0U));
    }
    {
        using bth::RelayMode;
        Disk modes; modes.on = true;
        Store store(modes, events); Output output(events);
        bth::RelayController relay(output, store);
        REQUIRE(relay.init() == Status::ok && relay.start() == Status::ok && output.on);
        REQUIRE(relay.set_mode(RelayMode::unused, true) == Status::ok && !output.on);
        REQUIRE(relay.set(true) == Status::invalid_state && !output.on);
        REQUIRE(relay.set_mode(RelayMode::outage, true) == Status::ok && output.on);
        REQUIRE(relay.automatic(false) == Status::ok && !output.on);
        REQUIRE(relay.automatic(true) == Status::ok && output.on);
        REQUIRE(modes.writes == 0 && modes.on);
        relay.deinit();
        REQUIRE(relay.init() == Status::ok && relay.start() == Status::ok && !output.on);
        REQUIRE(relay.state().mode == RelayMode::outage);
        REQUIRE(relay.set_mode(RelayMode::manual, false) == Status::ok && output.on);
        REQUIRE(relay.set_mode(static_cast<RelayMode>(3), false) == Status::invalid_argument);
        store.fail_save = true;
        REQUIRE(relay.set_mode(RelayMode::unused, false) == Status::hardware_error);
        REQUIRE(!relay.state().ready && !relay.state().storage_known);
        REQUIRE(relay.state().mode == RelayMode::manual);
        relay.deinit();
    }

}
