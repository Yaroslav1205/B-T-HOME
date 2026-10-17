#include "bth/platform/wifi_ap.hpp"
#include <cstring>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "esp_mac.h"
namespace bth {
namespace {
Status check(esp_err_t result) noexcept {
    if (result == ESP_OK) { return Status::ok; }
    ESP_LOGE("wifi_ap", "%s", esp_err_to_name(result));
    return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
// These callbacks only log SDK events. They never call Runtime or modify module state.
void ap_event(void* context, esp_event_base_t base, std::int32_t id, void* data) {
    auto* netif = static_cast<esp_netif_t*>(context);
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        esp_netif_ip_info_t ip{};
        esp_netif_dhcp_status_t status{};
        const auto ip_result = esp_netif_get_ip_info(netif, &ip);
        const auto dhcp_result = esp_netif_dhcps_get_status(netif, &status);
        if (ip_result == ESP_OK && dhcp_result == ESP_OK && status == ESP_NETIF_DHCP_STARTED)
            ESP_LOGI("wifi_ap", "AP ready | IP=" IPSTR " | DHCP started", IP2STR(&ip.ip));
        else ESP_LOGE("wifi_ap", "AP not ready: IP=%s DHCP=%s state=%d",
                      esp_err_to_name(ip_result), esp_err_to_name(dhcp_result), static_cast<int>(status));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const auto& event = *static_cast<wifi_event_ap_staconnected_t*>(data);
        ESP_LOGI("wifi_ap", "Client joined: " MACSTR " aid=%u; waiting for DHCP", MAC2STR(event.mac), event.aid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const auto& event = *static_cast<wifi_event_ap_stadisconnected_t*>(data);
        ESP_LOGW("wifi_ap", "Client left: " MACSTR " reason=%u", MAC2STR(event.mac), event.reason);
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        const auto& event = *static_cast<ip_event_ap_staipassigned_t*>(data);
        if (event.esp_netif == netif)
            ESP_LOGI("wifi_ap", "DHCP assigned " IPSTR " to " MACSTR, IP2STR(&event.ip), MAC2STR(event.mac));
    }
}
esp_err_t configure_ap_network(esp_netif_t* netif) noexcept {
    auto result = esp_netif_dhcps_stop(netif);
    if (result != ESP_OK && result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return result;
    esp_netif_ip_info_t ip{};
    ip.ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ip.gw = ip.ip;
    ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    result = esp_netif_set_ip_info(netif, &ip);
    if (result != ESP_OK) return result;
    // While the interface is down, this arms DHCP for the default AP_START handler.
    return esp_netif_dhcps_start(netif);
}
esp_err_t configure(const WifiSettings& settings) noexcept {
    wifi_config_t config{};
    std::memcpy(config.ap.ssid, settings.ssid, std::strlen(settings.ssid));
    config.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(settings.ssid));
    std::memcpy(config.ap.password, settings.password, std::strlen(settings.password));
    config.ap.channel = 1;
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    config.ap.pmf_cfg.required = false;
    config.ap.beacon_interval = 100;
    config.ap.dtim_period = 1;
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
    const esp_netif_config_t ap_config = ESP_NETIF_DEFAULT_WIFI_AP();
    const esp_netif_config_t sta_config = ESP_NETIF_DEFAULT_WIFI_STA();
    const auto& netif_config = station_ ? sta_config : ap_config;
    netif_ = esp_netif_new(&netif_config);
    if (!netif_) { return Status::no_memory; }
    result = station_ ? esp_netif_attach_wifi_station(netif_) : esp_netif_attach_wifi_ap(netif_);
    if (result != ESP_OK) { return check(result); }
    result = station_ ? esp_wifi_set_default_wifi_sta_handlers() : esp_wifi_set_default_wifi_ap_handlers();
    if (result != ESP_OK) { return check(result); }
    if (!station_) {
        result = configure_ap_network(netif_);
        if (result != ESP_OK) return check(result);
        // Registered after the default handlers so AP_START observes the initialized netif.
        result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, ap_event, netif_, &wifi_events_);
        if (result != ESP_OK) return check(result);
        result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, ap_event, netif_, &ip_events_);
        if (result != ESP_OK) return check(result);
    }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) { return check(result); }
    initialized_ = true;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) { return check(result); }
    active_ = settings_.config().wifi;
    stations_ = settings_.config().stations;
    retry_.reset(stations_[0].ssid[0], stations_[1].ssid[0]);
    connected_ = false;
    result = esp_wifi_set_mode(station_ ? WIFI_MODE_STA : WIFI_MODE_AP);
    if (result != ESP_OK) { return check(result); }
    return station_ ? Status::ok : check(configure(active_));
#endif
}
Status WifiAp::start() noexcept {
    if (!initialized_) { return Status::invalid_state; }
    // stop() must handle a partially failed start as well.
    started_ = true;
    const auto result = esp_wifi_start();
    if (result == ESP_OK && !station_) {
        ESP_LOGI("wifi_ap", "SSID: %s | http://192.168.4.1", active_.ssid);
    }
    return check(result);
}
std::array<std::uint8_t, 4> WifiAp::ipv4_address() const noexcept {
    esp_netif_ip_info_t info{};
    if (!netif_ || (station_ && !connected_) || esp_netif_get_ip_info(netif_, &info) != ESP_OK) return {};
    const auto ip = esp_netif_htonl(info.ip.addr);
    return {static_cast<std::uint8_t>(ip >> 24), static_cast<std::uint8_t>(ip >> 16),
            static_cast<std::uint8_t>(ip >> 8), static_cast<std::uint8_t>(ip)};
}
Status WifiAp::tick(std::uint64_t now) noexcept {
    if (!started_) return Status::invalid_state;
    if (!station_) return Status::ok;
    wifi_ap_record_t ap{};
    esp_netif_ip_info_t info{};
    const bool online = esp_wifi_sta_get_ap_info(&ap) == ESP_OK && esp_netif_is_netif_up(netif_) &&
        esp_netif_get_ip_info(netif_, &info) == ESP_OK && info.ip.addr != 0;
    if (online && !connected_) ESP_LOGI("wifi_sta", "Connected | http://" IPSTR, IP2STR(&info.ip));
    connected_ = online;
    const int slot = retry_.poll(now, online);
    if (slot < 0) return Status::ok;
    (void)esp_wifi_disconnect();
    wifi_config_t config{};
    const auto& network = stations_[static_cast<unsigned>(slot)];
    std::memcpy(config.sta.ssid, network.ssid, std::strlen(network.ssid));
    std::memcpy(config.sta.password, network.password, std::strlen(network.password));
    config.sta.threshold.authmode = network.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    const auto configured = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (configured != ESP_OK) return check(configured);
    ESP_LOGI("wifi_sta", "Trying network %d: %s", slot + 1, network.ssid);
    // Association/authentication failures are normal; retry the next slot after the deadline.
    const auto result = esp_wifi_connect();
    if (result != ESP_OK) ESP_LOGW("wifi_sta", "Connect attempt: %s", esp_err_to_name(result));
    return Status::ok;
}
Status WifiAp::apply(const WifiSettings& settings) noexcept {
    if (!initialized_ || !started_ || !valid_wifi(settings)) { return Status::invalid_state; }
    if (station_) return Status::ok;
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
    connected_ = false;
    if (started_) { (void)check(esp_wifi_stop()); started_ = false; }
}
void WifiAp::deinit() noexcept {
    stop();
    if (initialized_) { (void)check(esp_wifi_deinit()); initialized_ = false; }
    if (wifi_events_) {
        (void)check(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_events_));
        wifi_events_ = nullptr;
    }
    if (ip_events_) {
        (void)check(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, ip_events_));
        ip_events_ = nullptr;
    }
    if (netif_) { esp_netif_destroy_default_wifi(netif_); netif_ = nullptr; }
    if (event_loop_) { (void)check(esp_event_loop_delete_default()); event_loop_ = false; }
    // ESP-IDF's process-wide lwIP stack cannot be deinitialized; init is idempotent.
    active_ = {};
    stations_ = {};
}
}  // namespace bth
