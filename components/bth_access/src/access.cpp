#include "bth/access/access.hpp"
#include <cstdio>
#include <cstring>
namespace bth {
namespace {
bool text(const char* value, std::size_t min, std::size_t max, bool ascii) noexcept {
    if (!value) { return false; }
    for (std::size_t i = 0; i <= max; ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c == 0) { return i >= min; }
        if (c < 32 || c == 127 || (ascii && c > 126)) { return false; }
    }
    return false;
}
}
bool valid_wifi(const WifiSettings& wifi) noexcept {
    return text(wifi.ssid, 1, 32, false) && text(wifi.password, 8, 63, true);
}
bool valid_username(const char* value) noexcept { return text(value, 1, 32, true); }
bool valid_web_password(const char* value) noexcept { return text(value, 1, 63, false); }
bool same_wifi(const WifiSettings& a, const WifiSettings& b) noexcept {
    return std::strcmp(a.ssid, b.ssid) == 0 && std::strcmp(a.password, b.password) == 0;
}
void default_wifi(WifiSettings& wifi, const std::array<unsigned char, 6>& mac) noexcept {
    wifi = {};
    std::snprintf(wifi.ssid, sizeof(wifi.ssid), "B-T HOME %02X%02X",
                  static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    std::snprintf(wifi.password, sizeof(wifi.password), "%s", "12345678");
}
Status AccessSettings::init() noexcept {
    if (acquired_) { return Status::invalid_state; }
    acquired_ = true;
    auto result = store_.init();
    if (result != Status::ok) { return result; }
    bool found = false;
    result = store_.load(config_, found);
    if (result != Status::ok) { return result; }
    if (!found) {
        config_ = {};
        default_wifi(config_.wifi, mac_);
        std::snprintf(config_.web.username, sizeof(config_.web.username), "%s", "admin");
        result = codec_.create("admin", config_.web);
        if (result == Status::ok) { result = store_.save(config_); }
        if (result != Status::ok) { return result; }
    }
    if (config_.version != 1 || !valid_wifi(config_.wifi) || !valid_username(config_.web.username)) {
        return Status::invalid_argument;
    }
    ready_ = true;
    return Status::ok;
}
bool AccessSettings::authenticate(const char* username, const char* password) noexcept {
    if (!ready_ || !valid_username(username) || !valid_web_password(password)) { return false; }
    const bool matches = codec_.verify(password, config_.web);
    return matches && std::strcmp(username, config_.web.username) == 0;
}
Status AccessSettings::update(const WifiSettings& wifi, const char* username,
                              const char* new_password, bool& credentials_changed) noexcept {
    credentials_changed = false;
    if (!ready_) { return Status::invalid_state; }
    if (!valid_wifi(wifi) || !valid_username(username) || !new_password ||
        (new_password[0] && !valid_web_password(new_password))) {
        return Status::invalid_argument;
    }
    auto candidate = config_;
    candidate.wifi = wifi;
    std::snprintf(candidate.web.username, sizeof(candidate.web.username), "%s", username);
    const bool new_login = std::strcmp(config_.web.username, username) != 0 || new_password[0];
    if (new_password[0]) {
        const auto result = codec_.create(new_password, candidate.web);
        if (result != Status::ok) { return result; }
    }
    if (!new_login && same_wifi(config_.wifi, wifi)) { return Status::ok; }
    const auto result = store_.save(candidate);
    if (result != Status::ok) {
        // Failed commit can have uncertain durability. Fail closed until restart.
        ready_ = false;
        return result;
    }
    config_ = candidate;
    credentials_changed = new_login;
    return Status::ok;
}
void AccessSettings::deinit() noexcept {
    if (acquired_) { store_.deinit(); }
    acquired_ = false;
    ready_ = false;
    config_ = {};
}
}  // namespace bth
