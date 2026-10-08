#include "bth/radio/radio_mode.hpp"
#include "bth/radio/button.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK_R(c) do { if (!(c)) { std::cerr << "radio:" << __LINE__ << ": " #c "\n"; std::exit(EXIT_FAILURE); } } while(false)
namespace {
struct FakeRadio final : bth::Module {
    FakeRadio(const char* name, std::vector<std::string>& calls) : name(name), calls(calls) {}
    const char* name;
    std::vector<std::string>& calls;
    bth::Status init_result = bth::Status::ok, start_result = bth::Status::ok, tick_result = bth::Status::ok;
    const char* id() const noexcept override { return name; }
    void log(const char* operation) { calls.push_back(std::string(name) + operation); }
    bth::Status init() noexcept override { log(".init"); return init_result; }
    bth::Status start() noexcept override { log(".start"); return start_result; }
    bth::Status tick(std::uint64_t) noexcept override { log(".tick"); return tick_result; }
    void stop() noexcept override { log(".stop"); }
    void deinit() noexcept override { log(".deinit"); }
};
struct ButtonDriver {
    bth::BootGesture gesture;
    std::uint64_t now = 0;
    bth::ButtonAction sample(bool pressed, unsigned advance = 10) {
        now += advance; return gesture.update(pressed, now);
    }
    bth::ButtonAction press(unsigned duration) {
        CHECK_R(sample(true) == bth::ButtonAction::none);
        for (unsigned elapsed = 10; elapsed < duration; elapsed += 10)
            CHECK_R(sample(true) == bth::ButtonAction::none);
        CHECK_R(sample(false) == bth::ButtonAction::none);
        for (unsigned i = 0; i < 3; ++i) CHECK_R(sample(false) == bth::ButtonAction::none);
        return sample(false);
    }
};
}
void test_radio() {
    using bth::Status;
    std::vector<std::string> calls;
    FakeRadio zigbee("zb", calls), wifi("wifi", calls), web("web", calls);
    bth::RadioMode radio(zigbee, wifi, web);
    CHECK_R(radio.toggle() == Status::invalid_state);
    CHECK_R(radio.init() == Status::ok && radio.start() == Status::ok);
    CHECK_R(!radio.configuration_mode());
    CHECK_R(radio.tick(0) == Status::ok);
    calls.clear();
    CHECK_R(radio.toggle() == Status::ok && radio.configuration_mode());
    CHECK_R((calls == std::vector<std::string>{"zb.stop", "zb.deinit", "wifi.init", "web.init", "wifi.start", "web.start"}));
    calls.clear();
    CHECK_R(radio.tick(1) == Status::ok);
    CHECK_R((calls == std::vector<std::string>{"wifi.tick", "web.tick"}));
    calls.clear();
    CHECK_R(radio.toggle() == Status::ok && !radio.configuration_mode());
    CHECK_R((calls == std::vector<std::string>{"web.stop", "wifi.stop", "web.deinit", "wifi.deinit", "zb.init", "zb.start"}));
    zigbee.tick_result = Status::hardware_error;
    CHECK_R(radio.tick(2) == Status::hardware_error);
    radio.deinit(); calls.clear(); radio.stop(); radio.deinit(); CHECK_R(calls.empty());
    // Every partial init/start failure cleans attempted children in reverse order.
    for (unsigned failure = 0; failure < 6; ++failure) {
        zigbee.init_result = zigbee.start_result = wifi.init_result = wifi.start_result =
            web.init_result = web.start_result = Status::ok;
        bth::RadioMode failing(zigbee, wifi, web);
        if (failure < 2) {
            (failure == 0 ? zigbee.init_result : zigbee.start_result) = Status::hardware_error;
            const auto result = failing.init();
            CHECK_R((failure == 0 ? result : failing.start()) == Status::hardware_error);
        } else {
            CHECK_R(failing.init() == Status::ok && failing.start() == Status::ok);
            calls.clear();
            auto& result = failure == 2 ? wifi.init_result : failure == 3 ? web.init_result :
                           failure == 4 ? wifi.start_result : web.start_result;
            result = Status::hardware_error;
            CHECK_R(failing.toggle() == Status::hardware_error);
            CHECK_R(calls[0] == "zb.stop" && calls[1] == "zb.deinit");
            CHECK_R(calls.back() == "wifi.deinit");
            if (failure == 3) CHECK_R(calls[calls.size() - 2] == "web.deinit");
            if (failure == 5) CHECK_R(calls[6] == "web.stop" && calls[7] == "wifi.stop");
        }
        calls.clear(); failing.stop(); failing.deinit(); CHECK_R(calls.empty());
    }
    ButtonDriver button;
    CHECK_R(button.sample(false) == bth::ButtonAction::none);
    CHECK_R(button.press(100) == bth::ButtonAction::pair);
    CHECK_R(button.press(2990) == bth::ButtonAction::pair);
    CHECK_R(button.press(3000) == bth::ButtonAction::toggle_mode);
    CHECK_R(button.press(5000) == bth::ButtonAction::toggle_mode);
    CHECK_R(button.sample(false) == bth::ButtonAction::none);
    CHECK_R(button.press(20) == bth::ButtonAction::none);
    // A held button during startup is ignored until released, then accepts a new press.
    ButtonDriver held;
    CHECK_R(held.press(4000) == bth::ButtonAction::none);
    CHECK_R(held.press(100) == bth::ButtonAction::pair);
    // Missing samples cannot turn an uncertain press into a mode switch.
    CHECK_R(button.sample(true, 1000) == bth::ButtonAction::none);
    CHECK_R(button.press(4000) == bth::ButtonAction::none);
    CHECK_R(button.press(100) == bth::ButtonAction::pair);
    ButtonDriver uptime;
    uptime.now = (1ULL << 32);
    CHECK_R(uptime.sample(false) == bth::ButtonAction::none);
    CHECK_R(uptime.press(3000) == bth::ButtonAction::toggle_mode);
}
