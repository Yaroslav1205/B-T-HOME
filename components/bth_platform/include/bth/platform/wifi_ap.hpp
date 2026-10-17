#pragma once
#include "bth/core/module.hpp"
#include "esp_netif.h"
#include "esp_event.h"
#include "bth/access/access.hpp"
#include "bth/radio/radio_mode.hpp"
#include "bth/radio/station_retry.hpp"
namespace bth {
class WifiAp final : public Module, public WifiModeControl {
public:
    explicit WifiAp(AccessSettings& settings) noexcept : settings_(settings) {}
    const char* id() const noexcept override { return "wifi"; }
    Dependencies dependencies() const noexcept override { return {required_, 2}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void select_station(bool station) noexcept override { if (!initialized_) station_ = station; }
    bool station_mode() const noexcept { return station_; }
    bool connected() const noexcept { return connected_; }
    std::array<std::uint8_t, 4> ipv4_address() const noexcept;
    // Only the HTTP owner calls apply while running; web joins before Wi-Fi stops.
    Status apply(const WifiSettings& settings) noexcept;
    const WifiSettings& active() const noexcept { return active_; }
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    inline static constexpr const char* required_[] = {"nvs", "access"};
    esp_netif_t* netif_ = nullptr;
    esp_event_handler_instance_t wifi_events_ = nullptr, ip_events_ = nullptr;
    bool event_loop_ = false;
    bool initialized_ = false;
    bool started_ = false;
    AccessSettings& settings_;
    WifiSettings active_{};
    std::array<WifiSettings, 2> stations_{};
    StationRetry retry_;
    bool station_ = false, connected_ = false;
};
}  // namespace bth
