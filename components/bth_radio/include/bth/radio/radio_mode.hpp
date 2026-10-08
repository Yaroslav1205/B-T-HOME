#pragma once
#include "bth/core/module.hpp"

namespace bth {
// Owns the lifecycle of mutually exclusive radio groups, not the objects.
// Providers (NVS, access and relay) belong to the outer Runtime and stay running.
class RadioMode final : public Module {
public:
    RadioMode(Module& zigbee, Module& wifi, Module& web) noexcept
        : zigbee_(zigbee), wifi_(wifi), web_(web) {}
    const char* id() const noexcept override { return "radio"; }
    Dependencies dependencies() const noexcept override { return {required_, 3}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    // Owner task only, outside tick: stopping children may join their workers.
    Status toggle() noexcept;
    bool configuration_mode() const noexcept { return configuration_; }
private:
    inline static constexpr const char* required_[] = {"nvs", "access", "relay"};
    Module& child(unsigned index) noexcept { return configuration_ ? (index == 0 ? wifi_ : web_) : zigbee_; }
    Module& zigbee_;
    Module& wifi_;
    Module& web_;
    bool configuration_ = false;
    unsigned initialized_ = 0, started_ = 0;
};
}  // namespace bth
