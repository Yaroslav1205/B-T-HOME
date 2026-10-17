#include "bth/platform/access_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::cerr << "access store:" << __LINE__ << ": " #c "\n"; std::exit(1); } } while(false)
namespace {
std::map<std::string, std::vector<unsigned char>> disk, staged;
bool opened = false, mode_found = false;
std::uint8_t mode_value = 0, staged_mode = 0;
unsigned commits = 0;
esp_err_t commit_result = ESP_OK;
}
const char* esp_err_to_name(esp_err_t) { return "test_error"; }
esp_err_t nvs_open(const char*, int, nvs_handle_t* handle) { CHECK(!opened); opened = true; *handle = 1; return ESP_OK; }
void nvs_close(nvs_handle_t) { CHECK(opened); opened = false; staged.clear(); }
esp_err_t nvs_get_blob(nvs_handle_t, const char* key, void* data, std::size_t* length) {
    CHECK(opened);
    const auto entry = disk.find(key);
    if (entry == disk.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < entry->second.size()) return ESP_FAIL;
    *length = entry->second.size();
    std::memcpy(data, entry->second.data(), *length); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t, const char* key, const void* data, std::size_t length) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    staged[key] = std::vector<unsigned char>(bytes, bytes + length); return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t, const char*, std::uint8_t* value) {
    if (!mode_found) return ESP_ERR_NVS_NOT_FOUND;
    *value = mode_value; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t, const char*, std::uint8_t value) { staged_mode = value; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t) {
    ++commits;
    if (commit_result != ESP_OK) return commit_result;
    for (const auto& entry : staged) disk[entry.first] = entry.second;
    staged.clear();
    if (staged_mode) { mode_value = staged_mode; mode_found = true; staged_mode = 0; }
    return ESP_OK;
}
// Crypto is outside these persistence tests; never substitute these stubs in firmware.
void esp_fill_random(void* data, std::size_t size) { std::memset(data, 0, size); }
void mbedtls_platform_zeroize(void* data, std::size_t size) { std::memset(data, 0, size); }
int mbedtls_pkcs5_pbkdf2_hmac_ext(int, const unsigned char*, std::size_t,
    const unsigned char*, std::size_t, unsigned int, unsigned int, unsigned char*) { return -1; }
int main() {
    using bth::Status;
    bth::NvsAccessStore store;
    CHECK(store.init() == Status::ok);
    bth::AccessConfig config{}; bool found = true;
    CHECK(store.load(config, found) == Status::ok && !found && commits == 0);
    struct Legacy { std::uint32_t version = 1; bth::WifiSettings wifi{}; bth::WebCredentials web{}; } legacy;
    static_assert(sizeof(Legacy) == 184);
    bth::default_wifi(legacy.wifi, {});
    std::snprintf(legacy.web.username, sizeof(legacy.web.username), "%s", "existing-owner");
    legacy.web.salt.fill(42); legacy.web.hash.fill(73);
    const auto* bytes = reinterpret_cast<const unsigned char*>(&legacy);
    disk["config_v1"] = {bytes, bytes + sizeof(legacy)};
    CHECK(store.load(config, found) == Status::ok && found && commits == 1);
    CHECK(config.version == 2 && bth::same_wifi(config.wifi, legacy.wifi));
    CHECK(config.web.hash == legacy.web.hash && config.web.salt == legacy.web.salt);
    CHECK(std::strcmp(config.web.username, legacy.web.username) == 0);
    CHECK(!config.stations[0].ssid[0] && !config.stations[1].ssid[0]);
    std::snprintf(config.stations[0].ssid, sizeof(config.stations[0].ssid), "%s", "router");
    CHECK(store.save(config) == Status::ok);
    store.deinit(); CHECK(store.init() == Status::ok);
    bth::AccessConfig restored{};
    CHECK(store.load(restored, found) == Status::ok && found && commits == 2);
    CHECK(bth::same_wifi(restored.stations[0], config.stations[0]));
    bth::RadioSelection mode{};
    CHECK(store.load_mode(mode) == Status::ok && mode == bth::RadioSelection::ap);
    CHECK(store.save_mode(bth::RadioSelection::station) == Status::ok);
    CHECK(store.load_mode(mode) == Status::ok && mode == bth::RadioSelection::station);
    mode_value = 9; CHECK(store.load_mode(mode) == Status::invalid_argument);
    commit_result = ESP_FAIL;
    CHECK(store.save_mode(bth::RadioSelection::zigbee) == Status::hardware_error);
    disk.erase("config_v2");
    CHECK(store.load(restored, found) == Status::hardware_error && found);
    CHECK(disk.find("config_v2") == disk.end());
    store.deinit(); CHECK(store.init() == Status::ok); commit_result = ESP_OK;
    disk["config_v2"] = {1, 2};
    const auto previous = commits;
    CHECK(store.load(restored, found) == Status::invalid_argument && found && commits == previous);
    disk.erase("config_v2"); disk["config_v1"][0] = 99;
    CHECK(store.load(restored, found) == Status::invalid_argument && commits == previous);
    store.deinit(); store.deinit(); CHECK(!opened);
    std::cout << "Access migration and radio persistence passed\n";
}
