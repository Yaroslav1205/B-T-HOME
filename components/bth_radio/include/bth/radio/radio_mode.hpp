#pragma once
#include "bth/core/module.hpp"

namespace bth {
enum class RadioSelection : std::uint8_t { ap = 1, station = 2, zigbee = 3 };
class RadioModeStore {
public:
    virtual ~RadioModeStore() = default;
    virtual Status load_mode(RadioSelection& mode) noexcept = 0;
    virtual Status save_mode(RadioSelection mode) noexcept = 0;
};
class WifiModeControl {
public:
    virtual ~WifiModeControl() = default;
    virtual void select_station(bool station) noexcept = 0;
};
// Owns the lifecycle of mutually exclusive radio groups, not the objects.
// Providers (NVS, access and relay) belong to the outer Runtime and stay running.
class RadioMode final : public Module {
public:
    RadioMode(Module& zigbee, Module& wifi, Module& web, WifiModeControl& control, RadioModeStore& store) noexcept
        : zigbee_(zigbee), wifi_(wifi), web_(web), control_(control), store_(store) {}
    const char* id() const noexcept override { return "radio"; }
    Dependencies dependencies() const noexcept override { return {required_, 3}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    // Owner task only, outside tick: stopping children may join their workers.
    Status toggle() noexcept;
    bool configuration_mode() const noexcept { return mode_ != RadioSelection::zigbee; }
    RadioSelection selection() const noexcept { return mode_; }
private:
    inline static constexpr const char* required_[] = {"nvs", "access", "relay"};
    Module& child(unsigned index) noexcept { return configuration_mode() ? (index == 0 ? wifi_ : web_) : zigbee_; }
    Module& zigbee_;
    Module& wifi_;
    Module& web_;
    WifiModeControl& control_;
    RadioModeStore& store_;
    RadioSelection mode_ = RadioSelection::ap;
    bool loaded_ = false;
    unsigned initialized_ = 0, started_ = 0;
};
}  // namespace bth
