#pragma once
#include "esp_netif.h"
#include "esp_event.h"
constexpr int WIFI_AUTH_OPEN=0, WIFI_AUTH_WPA2_PSK=2, WIFI_CIPHER_TYPE_CCMP=4,
 WIFI_IF_AP=1, WIFI_IF_STA=0, WIFI_MODE_AP=1, WIFI_MODE_STA=0, WIFI_STORAGE_RAM=0;
struct wifi_init_config_t { int unused=0; };
#define WIFI_INIT_CONFIG_DEFAULT() {}
struct wifi_config_t {
 struct { unsigned char ssid[32]{},password[64]{}; unsigned ssid_len=0,channel=0,max_connection=0;
 int authmode=0,pairwise_cipher=0; struct {bool required=false;} pmf_cfg;
 unsigned beacon_interval=0,dtim_period=0; } ap;
 struct {unsigned char ssid[32]{},password[64]{}; struct {int authmode=0;} threshold;} sta;
};
struct wifi_ap_record_t {};
struct wifi_event_ap_staconnected_t {std::uint8_t mac[6]{}; unsigned aid=0;};
struct wifi_event_ap_stadisconnected_t {std::uint8_t mac[6]{}; unsigned aid=0,reason=0;};
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_deinit();
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t*);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);
esp_err_t esp_wifi_disconnect();
esp_err_t esp_wifi_connect();
