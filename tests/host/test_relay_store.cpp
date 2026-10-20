#include "bth/platform/nvs_storage.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#define CHECK(c) do { if (!(c)) { std::cerr << __LINE__ << ": " #c; std::exit(1); } } while(false)
namespace { bool present=false, opened=false; std::uint8_t persisted=1, staged=1; esp_err_t commit=ESP_OK, load_error=ESP_OK; }
const char* esp_err_to_name(esp_err_t) { return "test"; }
esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_deinit() { return ESP_OK; }
esp_err_t nvs_open(const char* name,int,nvs_handle_t* h) { CHECK(std::strcmp(name,"bth_relay")==0 && !opened); opened=true;*h=1;return ESP_OK; }
void nvs_close(nvs_handle_t) { opened=false; }
esp_err_t nvs_get_u8(nvs_handle_t,const char* key,std::uint8_t* value) {
    if(std::strcmp(key,"on")==0) { *value=1;return ESP_OK; }
    CHECK(std::strcmp(key,"mode")==0);
    if(load_error!=ESP_OK)return load_error;
    if(!present)return ESP_ERR_NVS_NOT_FOUND;
    *value=persisted;return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t,const char* key,std::uint8_t value) {
    CHECK(std::strcmp(key,"mode")==0);staged=value;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { if(commit==ESP_OK){persisted=staged;present=true;}return commit; }
int main() {
    using bth::RelayMode;using bth::Status;
    bth::NvsRelayStore store;RelayMode mode=RelayMode::unused;
    CHECK(store.load_mode(mode)==Status::invalid_state);
    CHECK(store.init()==Status::ok);
    CHECK(store.load_mode(mode)==Status::ok && mode==RelayMode::manual);
    for(auto selected:{RelayMode::unused,RelayMode::manual,RelayMode::outage}) {
        CHECK(store.save_mode(selected)==Status::ok);
        store.deinit();CHECK(store.init()==Status::ok);
        CHECK(store.load_mode(mode)==Status::ok && mode==selected);
        bool on=false;CHECK(store.load(on)==Status::ok && on);
    }
    CHECK(store.save_mode(static_cast<RelayMode>(3))==Status::invalid_argument);
    commit=ESP_FAIL;CHECK(store.save_mode(RelayMode::unused)==Status::hardware_error);
    CHECK(store.load_mode(mode)==Status::ok && mode==RelayMode::outage);
    persisted=3;CHECK(store.load_mode(mode)==Status::invalid_argument);
    load_error=ESP_FAIL;CHECK(store.load_mode(mode)==Status::hardware_error);
    store.deinit();store.deinit();CHECK(!opened);
    std::cout << "PASS: relay mode NVS migration, persistence, bounds and errors\n";
}
