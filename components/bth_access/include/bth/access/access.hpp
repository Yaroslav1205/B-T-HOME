#pragma once
#include <array>
#include "bth/core/module.hpp"
namespace bth {
struct WifiSettings {
    char ssid[33]{};
    char password[64]{};
};
struct WebCredentials {
    char username[33]{};
    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 32> hash{};
};
struct AccessConfig {
    std::uint32_t version = 1;
    WifiSettings wifi{};
    WebCredentials web{};
};
bool valid_wifi(const WifiSettings& wifi) noexcept;
bool valid_username(const char* value) noexcept;
bool valid_web_password(const char* value) noexcept;
bool same_wifi(const WifiSettings& a, const WifiSettings& b) noexcept;
void default_wifi(WifiSettings& wifi, const std::array<unsigned char, 6>& mac) noexcept;

class AccessStore {
public:
    virtual ~AccessStore() = default;
    virtual Status init() noexcept = 0;
    virtual Status load(AccessConfig& config, bool& found) noexcept = 0;
    virtual Status save(const AccessConfig& config) noexcept = 0;
    virtual void deinit() noexcept = 0;
};
class PasswordCodec {
public:
    virtual ~PasswordCodec() = default;
    virtual Status create(const char* password, WebCredentials& credentials) noexcept = 0;
    virtual bool verify(const char* password, const WebCredentials& credentials) noexcept = 0;
};
// Startup runs in the Runtime owner; thereafter only the joined HTTP task accesses settings.
class AccessSettings final : public Module {
public:
    AccessSettings(AccessStore& store, PasswordCodec& codec,
                   std::array<unsigned char, 6> mac) noexcept
        : store_(store), codec_(codec), mac_(mac) {}
    const char* id() const noexcept override { return "access"; }
    Dependencies dependencies() const noexcept override { return {required_, 1}; }
    Status init() noexcept override;
    Status start() noexcept override { return ready_ ? Status::ok : Status::invalid_state; }
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override {}
    void deinit() noexcept override;
    bool usable() const noexcept { return ready_; }
    const AccessConfig& config() const noexcept { return config_; }
    bool authenticate(const char* username, const char* password) noexcept;
    Status update(const WifiSettings& wifi, const char* username,
                  const char* new_password, bool& credentials_changed) noexcept;
private:
    inline static constexpr const char* required_[] = {"nvs"};
    AccessStore& store_;
    PasswordCodec& codec_;
    const std::array<unsigned char, 6> mac_;
    AccessConfig config_{};
    bool acquired_ = false;
    bool ready_ = false;
};
}  // namespace bth
