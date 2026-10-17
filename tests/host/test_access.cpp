#include "bth/access/access.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
namespace {
#define REQUIRE_ACCESS(condition) do { if (!(condition)) {     std::cerr << "access:" << __LINE__ << ": " #condition "\n";     std::exit(EXIT_FAILURE); } } while (false)
using bth::Status;
struct Store final : bth::AccessStore {
    bth::AccessConfig disk{};
    bool found = false;
    bool fail_init = false;
    bool fail_load = false;
    bool fail_save = false;
    int writes = 0;
    int closes = 0;
    Status init() noexcept override { return fail_init ? Status::hardware_error : Status::ok; }
    Status load(bth::AccessConfig& config, bool& present) noexcept override {
        config = disk; present = found;
        return fail_load ? Status::hardware_error : Status::ok;
    }
    Status save(const bth::AccessConfig& config) noexcept override {
        ++writes;
        if (fail_save) { return Status::hardware_error; }
        disk = config; found = true; return Status::ok;
    }
    void deinit() noexcept override { ++closes; }
};
// Deterministic test double, not a password hashing implementation.
struct Codec final : bth::PasswordCodec {
    bool fail = false;
    Status create(const char* password, bth::WebCredentials& credentials) noexcept override {
        if (fail) { return Status::internal_error; }
        credentials.hash.fill(0);
        for (std::size_t i = 0; password[i]; ++i) { credentials.hash[i % 32] ^= static_cast<unsigned char>(password[i]); }
        return Status::ok;
    }
    bool verify(const char* password, const bth::WebCredentials& credentials) noexcept override {
        bth::WebCredentials candidate{};
        return create(password, candidate) == Status::ok && candidate.hash == credentials.hash;
    }
};
constexpr std::array<unsigned char, 6> mac = {0x10, 0x20, 0x30, 0x40, 0x1B, 0xA4};
}
void test_access() {
    Store store; Codec codec;
    bth::AccessSettings access(store, codec, mac);
    REQUIRE_ACCESS(access.init() == Status::ok);
    REQUIRE_ACCESS(std::strcmp(access.config().wifi.ssid, "B-T HOME 1BA4") == 0);
    REQUIRE_ACCESS(std::strcmp(access.config().wifi.password, "12345678") == 0);
    REQUIRE_ACCESS(access.authenticate("admin", "admin"));
    REQUIRE_ACCESS(!access.authenticate("Admin", "admin"));
    REQUIRE_ACCESS(!access.authenticate("admin", "wrong"));
    REQUIRE_ACCESS(!access.authenticate(nullptr, "admin"));
    REQUIRE_ACCESS(store.writes == 1);
    bool changed = true;
    REQUIRE_ACCESS(access.update(access.config().wifi, "admin", "", changed) == Status::ok);
    REQUIRE_ACCESS(!changed && store.writes == 1);
    auto wifi = access.config().wifi;
    std::snprintf(wifi.ssid, sizeof(wifi.ssid), "%s", "Kitchen");
    std::snprintf(wifi.password, sizeof(wifi.password), "%s", "new-wifi-pass");
    REQUIRE_ACCESS(access.update(wifi, "owner", "new-web-pass", changed) == Status::ok && changed);
    REQUIRE_ACCESS(!access.authenticate("admin", "admin"));
    REQUIRE_ACCESS(access.authenticate("owner", "new-web-pass"));
    access.deinit(); access.deinit(); REQUIRE_ACCESS(store.closes == 1);
    REQUIRE_ACCESS(access.init() == Status::ok);
    REQUIRE_ACCESS(store.writes == 2 && access.authenticate("owner", "new-web-pass"));
    REQUIRE_ACCESS(bth::same_wifi(wifi, access.config().wifi));
    // Blank new web password preserves its hash while username can change.
    REQUIRE_ACCESS(access.update(wifi, "new-owner", "", changed) == Status::ok && changed);
    REQUIRE_ACCESS(access.authenticate("new-owner", "new-web-pass"));
    REQUIRE_ACCESS(!access.authenticate("owner", "new-web-pass"));
    auto invalid = wifi; std::snprintf(invalid.password, sizeof(invalid.password), "%s", "short");
    const auto writes = store.writes;
    REQUIRE_ACCESS(access.update(invalid, "new-owner", "", changed) == Status::invalid_argument);
    REQUIRE_ACCESS(store.writes == writes && !changed);
    std::memset(invalid.ssid, 'x', sizeof(invalid.ssid));
    REQUIRE_ACCESS(!bth::valid_wifi(invalid));
    REQUIRE_ACCESS(access.update(wifi, "", "", changed) == Status::invalid_argument);
    REQUIRE_ACCESS(access.update(wifi, "bad\nuser", "", changed) == Status::invalid_argument);
    codec.fail = true;
    REQUIRE_ACCESS(access.update(wifi, "new-owner", "change", changed) == Status::internal_error);
    REQUIRE_ACCESS(store.writes == writes);
    codec.fail = false; store.fail_save = true;
    REQUIRE_ACCESS(access.update(wifi, "owner", "changed", changed) == Status::hardware_error);
    REQUIRE_ACCESS(!changed && !access.usable());
    REQUIRE_ACCESS(!access.authenticate("new-owner", "new-web-pass"));
    REQUIRE_ACCESS(access.update(wifi, "owner", "changed", changed) == Status::invalid_state);
    access.deinit();
    // An unknown schema or malformed stored data is never overwritten with defaults.
    store.fail_save = false; store.disk.version = 99;
    const auto before = store.writes;
    REQUIRE_ACCESS(access.init() == Status::invalid_argument);
    REQUIRE_ACCESS(store.writes == before); access.deinit();
    for (int failure = 0; failure < 3; ++failure) {
        Store broken;
        broken.fail_init = failure == 0; broken.fail_load = failure == 1; broken.fail_save = failure == 2;
        bth::AccessSettings settings(broken, codec, mac);
        REQUIRE_ACCESS(settings.init() == Status::hardware_error);
        REQUIRE_ACCESS(!settings.usable());
        settings.deinit(); settings.deinit(); REQUIRE_ACCESS(broken.closes == 1);
    }
    {
        Store networks_store; Codec networks_codec;
        bth::AccessSettings networks(networks_store, networks_codec, mac);
        REQUIRE_ACCESS(networks.init() == Status::ok);
        std::array<bth::WifiSettings, 2> stations{};
        std::snprintf(stations[0].ssid, sizeof(stations[0].ssid), "%s", "Primary");
        std::snprintf(stations[0].password, sizeof(stations[0].password), "%s", "primary-password");
        std::snprintf(stations[1].ssid, sizeof(stations[1].ssid), "%s", "Open backup");
        bool credentials = true;
        REQUIRE_ACCESS(networks.update(networks.config().wifi, "admin", "", credentials, &stations) == Status::ok);
        REQUIRE_ACCESS(!credentials && networks_store.writes == 2);
        REQUIRE_ACCESS(networks.update(networks.config().wifi, "admin", "", credentials, &stations) == Status::ok);
        REQUIRE_ACCESS(networks_store.writes == 2);
        networks.deinit(); REQUIRE_ACCESS(networks.init() == Status::ok);
        REQUIRE_ACCESS(bth::same_wifi(networks.config().stations[0], stations[0]));
        REQUIRE_ACCESS(bth::same_wifi(networks.config().stations[1], stations[1]));
        std::snprintf(stations[1].password, sizeof(stations[1].password), "%s", "short");
        REQUIRE_ACCESS(networks.update(networks.config().wifi, "admin", "", credentials, &stations) == Status::invalid_argument);
        REQUIRE_ACCESS(networks_store.writes == 2);
        stations[1] = {};
        REQUIRE_ACCESS(networks.update(networks.config().wifi, "admin", "", credentials, &stations) == Status::ok);
        REQUIRE_ACCESS(!networks.config().stations[1].ssid[0]);
        networks_store.fail_save = true;
        stations[0] = {};
        REQUIRE_ACCESS(networks.update(networks.config().wifi, "admin", "", credentials, &stations) == Status::hardware_error);
        REQUIRE_ACCESS(!networks.usable());
        networks.deinit();
    }
    bth::WifiSettings leading_zero{};
    bth::default_wifi(leading_zero, {0, 0, 0, 0, 0, 1});
    REQUIRE_ACCESS(std::strcmp(leading_zero.ssid, "B-T HOME 0001") == 0);
    bth::WifiSettings maximum{};
    std::memset(maximum.ssid, 'S', 32); std::memset(maximum.password, 'p', 63);
    REQUIRE_ACCESS(bth::valid_wifi(maximum));
}
