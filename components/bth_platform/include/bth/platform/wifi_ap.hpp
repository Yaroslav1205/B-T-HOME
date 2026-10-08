#pragma once
#include "bth/core/module.hpp"
#include "esp_netif.h"
#include "bth/access/access.hpp"
namespace bth {
class WifiAp final : public Module {
public:
    explicit WifiAp(AccessSettings& settings) noexcept : settings_(settings) {}
    const char* id() const noexcept override { return "wifi"; }
    Dependencies dependencies() const noexcept override { return {required_, 2}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    // Only the HTTP owner calls apply while running; web joins before Wi-Fi stops.
    Status apply(const WifiSettings& settings) noexcept;
    const WifiSettings& active() const noexcept { return active_; }
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    inline static constexpr const char* required_[] = {"nvs", "access"};
    esp_netif_t* netif_ = nullptr;
    bool event_loop_ = false;
    bool initialized_ = false;
    bool started_ = false;
    AccessSettings& settings_;
    WifiSettings active_{};
};
}  // namespace bth
