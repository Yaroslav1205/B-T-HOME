#include "bth/platform/wifi_ap.hpp"
#include <cstring>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
namespace bth {
namespace {
Status check(esp_err_t result) noexcept {
    if (result == ESP_OK) { return Status::ok; }
    ESP_LOGE("wifi_ap", "%s", esp_err_to_name(result));
    return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
esp_err_t configure(const WifiSettings& settings) noexcept {
    wifi_config_t config{};
    std::memcpy(config.ap.ssid, settings.ssid, std::strlen(settings.ssid));
    config.ap.ssid_len = std::strlen(settings.ssid);
    std::memcpy(config.ap.password, settings.password, std::strlen(settings.password));
    config.ap.channel = 1;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_AP, &config);
}
}
Status WifiAp::init() noexcept {
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    return Status::invalid_state;
#else
    auto result = esp_netif_init();
    if (result != ESP_OK) { return check(result); }
    result = esp_event_loop_create_default();
    if (result != ESP_OK) { return check(result); }
    event_loop_ = true;
    const esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    netif_ = esp_netif_new(&netif_config);
    if (!netif_) { return Status::no_memory; }
    result = esp_netif_attach_wifi_ap(netif_);
    if (result != ESP_OK) { return check(result); }
    result = esp_wifi_set_default_wifi_ap_handlers();
    if (result != ESP_OK) { return check(result); }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) { return check(result); }
    initialized_ = true;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) { return check(result); }
    active_ = settings_.config().wifi;
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result != ESP_OK) { return check(result); }
    return check(configure(active_));
#endif
}
Status WifiAp::start() noexcept {
    if (!initialized_) { return Status::invalid_state; }
    // stop() must handle a partially failed start as well.
    started_ = true;
    const auto result = esp_wifi_start();
    if (result == ESP_OK) {
        ESP_LOGI("wifi_ap", "SSID: %s | http://192.168.4.1", active_.ssid);
    }
    return check(result);
}
Status WifiAp::apply(const WifiSettings& settings) noexcept {
    if (!initialized_ || !started_ || !valid_wifi(settings)) { return Status::invalid_state; }
    if (same_wifi(settings, active_)) { return Status::ok; }
    auto result = esp_wifi_stop();
    if (result != ESP_OK) { return check(result); }
    result = configure(settings);
    if (result == ESP_OK) { result = esp_wifi_start(); }
    if (result == ESP_OK) {
        active_ = settings;
        ESP_LOGI("wifi_ap", "SSID updated: %s", active_.ssid);
        return Status::ok;
    }
    (void)check(result);
    // Keep the last working network available if the live change fails.
    (void)esp_wifi_stop();
    (void)check(configure(active_));
    (void)check(esp_wifi_start());
    return Status::hardware_error;
}
void WifiAp::stop() noexcept {
    if (started_) { (void)check(esp_wifi_stop()); started_ = false; }
}
void WifiAp::deinit() noexcept {
    stop();
    if (initialized_) { (void)check(esp_wifi_deinit()); initialized_ = false; }
    if (netif_) { esp_netif_destroy_default_wifi(netif_); netif_ = nullptr; }
    if (event_loop_) { (void)check(esp_event_loop_delete_default()); event_loop_ = false; }
    // ESP-IDF's process-wide lwIP stack cannot be deinitialized; init is idempotent.
    active_ = {};
}
}  // namespace bth
