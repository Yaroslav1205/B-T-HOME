#include "bth/platform/wifi_ap.hpp"
#include "esp_wifi.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#define CHECK(c) do { if (!(c)) { std::cerr << "wifi:" << __LINE__ << ": " #c "\n"; std::exit(1); } } while(false)
namespace {
unsigned step=0, fail_at=0, handlers=0;
bool loop=false, driver=false, started=false;
esp_netif_t* net=nullptr;
esp_netif_dhcp_status_t dhcp=ESP_NETIF_DHCP_INIT;
esp_netif_ip_info_t ip{};
esp_event_handler_t callback=nullptr;
void* callback_context=nullptr;
esp_err_t operation() { return ++step == fail_at ? ESP_FAIL : ESP_OK; }
struct Store : bth::AccessStore {
 bth::Status init() noexcept override {return bth::Status::ok;}
 bth::Status load(bth::AccessConfig& config,bool& found) noexcept override {
  found=true;config={};bth::default_wifi(config.wifi,{});
  std::memcpy(config.web.username,"admin",6);return bth::Status::ok;
 }
 bth::Status save(const bth::AccessConfig&) noexcept override {return bth::Status::ok;}
 void deinit() noexcept override {}
};
struct Codec : bth::PasswordCodec {
 bth::Status create(const char*,bth::WebCredentials&) noexcept override {return bth::Status::ok;}
 bool verify(const char*,const bth::WebCredentials&) noexcept override {return true;}
};
}
const char* esp_err_to_name(esp_err_t) {return "test";}
esp_err_t esp_netif_init() {return operation();}
esp_err_t esp_event_loop_create_default() {auto e=operation();if(e==ESP_OK){CHECK(!loop);loop=true;}return e;}
esp_err_t esp_event_loop_delete_default() {CHECK(loop&&!net&&!handlers&&!driver);loop=false;return ESP_OK;}
esp_netif_t* esp_netif_new(const esp_netif_config_t* config) {
 if(operation()!=ESP_OK)return nullptr;
 CHECK(!net);net=new esp_netif_t{config->station};dhcp=ESP_NETIF_DHCP_INIT;return net;
}
void esp_netif_destroy_default_wifi(esp_netif_t* value) {CHECK(value==net&&!handlers&&!driver);delete net;net=nullptr;}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t*) {return operation();}
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t*) {return operation();}
esp_err_t esp_wifi_set_default_wifi_sta_handlers() {return operation();}
esp_err_t esp_wifi_set_default_wifi_ap_handlers() {return operation();}
esp_err_t esp_netif_dhcps_stop(esp_netif_t*) {auto e=operation();if(e==ESP_OK)dhcp=ESP_NETIF_DHCP_STOPPED;return e;}
esp_err_t esp_netif_set_ip_info(esp_netif_t*,const esp_netif_ip_info_t* value) {
 CHECK(dhcp==ESP_NETIF_DHCP_STOPPED);auto e=operation();if(e==ESP_OK)ip=*value;return e;
}
esp_err_t esp_netif_dhcps_start(esp_netif_t*) {
 CHECK(!net->up&&ip.ip.addr==ESP_IP4TOADDR(192,168,4,1));
 CHECK(ip.netmask.addr==ESP_IP4TOADDR(255,255,255,0)&&ip.gw.addr==ip.ip.addr);
 auto e=operation();if(e==ESP_OK)dhcp=ESP_NETIF_DHCP_INIT;return e;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t* value) {*value=ip;return ESP_OK;}
esp_err_t esp_netif_dhcps_get_status(esp_netif_t*,esp_netif_dhcp_status_t* value) {*value=dhcp;return ESP_OK;}
bool esp_netif_is_netif_up(esp_netif_t* value) {return value->up;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t base,int,esp_event_handler_t fn,void* context,esp_event_handler_instance_t* out) {
 auto e=operation();if(e==ESP_OK){++handlers;*out=reinterpret_cast<void*>(static_cast<std::uintptr_t>(handlers));
 if(base==WIFI_EVENT){callback=fn;callback_context=context;}}return e;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int,esp_event_handler_instance_t) {CHECK(net&&handlers);--handlers;return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t*) {auto e=operation();if(e==ESP_OK)driver=true;return e;}
esp_err_t esp_wifi_deinit() {CHECK(driver&&!started);driver=false;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int) {return operation();}
esp_err_t esp_wifi_set_mode(int mode) {CHECK((mode==WIFI_MODE_STA)==net->station);return operation();}
esp_err_t esp_wifi_set_config(int iface,const wifi_config_t* config) {
 if(iface==WIFI_IF_AP){CHECK(config->ap.authmode==WIFI_AUTH_WPA2_PSK&&config->ap.pairwise_cipher==WIFI_CIPHER_TYPE_CCMP);
 CHECK(!config->ap.pmf_cfg.required&&config->ap.beacon_interval==100&&config->ap.dtim_period==1);}
 return operation();
}
esp_err_t esp_wifi_start() {
 auto e=operation();if(e==ESP_OK){started=true;net->up=true;
 if(!net->station){CHECK(dhcp==ESP_NETIF_DHCP_INIT);dhcp=ESP_NETIF_DHCP_STARTED;callback(callback_context,WIFI_EVENT,WIFI_EVENT_AP_START,nullptr);}}
 return e;
}
esp_err_t esp_wifi_stop() {started=false;if(net)net->up=false;return ESP_OK;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*) {return ESP_FAIL;}
esp_err_t esp_wifi_disconnect() {return ESP_OK;}
esp_err_t esp_wifi_connect() {return ESP_OK;}
int main() {
 Store store;Codec codec;bth::AccessSettings settings(store,codec,{});
 CHECK(settings.init()==bth::Status::ok);
 for(bool station:{false,true}) {
  step=fail_at=0;
  bth::WifiAp wifi(settings);wifi.select_station(station);
  CHECK(wifi.init()==bth::Status::ok&&wifi.start()==bth::Status::ok);
  const auto count=step;
  CHECK(wifi.tick(0)==bth::Status::ok);
  if(!station)CHECK((wifi.ipv4_address()==std::array<std::uint8_t,4>{192,168,4,1}));
  wifi.deinit();wifi.deinit();CHECK(!net&&!loop&&!driver&&!handlers);
  for(unsigned failure=1;failure<=count;++failure){
   step=0;fail_at=failure;
   bth::WifiAp broken(settings);broken.select_station(station);
   auto result=broken.init();if(result==bth::Status::ok)result=broken.start();
   CHECK(result!=bth::Status::ok);broken.deinit();broken.deinit();
   CHECK(!net&&!loop&&!driver&&!handlers);
  }
 }
 std::cout<<"AP DHCP initialization and partial cleanup passed\n";
}
