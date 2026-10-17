#pragma once
#include <array>
#include <cstdint>
#include "esp_err.h"
struct esp_netif_t { bool station; bool up = false; };
struct esp_ip4_addr_t { std::uint32_t addr = 0; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
struct esp_netif_config_t { bool station; };
#define ESP_NETIF_DEFAULT_WIFI_AP() {false}
#define ESP_NETIF_DEFAULT_WIFI_STA() {true}
#define ESP_IP4TOADDR(a,b,c,d) ((std::uint32_t(a)<<24)|(std::uint32_t(b)<<16)|(std::uint32_t(c)<<8)|std::uint32_t(d))
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) unsigned((ip)->addr>>24),unsigned(((ip)->addr>>16)&255),unsigned(((ip)->addr>>8)&255),unsigned((ip)->addr&255)
inline std::uint32_t esp_netif_htonl(std::uint32_t value) { return value; }
enum esp_netif_dhcp_status_t { ESP_NETIF_DHCP_INIT, ESP_NETIF_DHCP_STARTED, ESP_NETIF_DHCP_STOPPED };
constexpr esp_err_t ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED = 0x5003;
struct ip_event_ap_staipassigned_t { esp_netif_t* esp_netif; esp_ip4_addr_t ip; std::uint8_t mac[6]; };
esp_err_t esp_netif_init();
esp_netif_t* esp_netif_new(const esp_netif_config_t*);
esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*);
esp_err_t esp_netif_set_ip_info(esp_netif_t*, const esp_netif_ip_info_t*);
esp_err_t esp_netif_dhcps_stop(esp_netif_t*);
esp_err_t esp_netif_dhcps_start(esp_netif_t*);
esp_err_t esp_netif_dhcps_get_status(esp_netif_t*, esp_netif_dhcp_status_t*);
bool esp_netif_is_netif_up(esp_netif_t*);
