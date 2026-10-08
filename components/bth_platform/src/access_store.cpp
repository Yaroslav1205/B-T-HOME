#include "bth/platform/access_store.hpp"
#include <cstring>
#include <type_traits>
#include "esp_random.h"
#include "esp_log.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
namespace bth {
namespace {
Status check(esp_err_t result) noexcept {
    if (result == ESP_OK) { return Status::ok; }
    ESP_LOGE("access", "%s", esp_err_to_name(result));
    return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
int derive(const char* password, const WebCredentials& credentials, unsigned char* hash) noexcept {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        reinterpret_cast<const unsigned char*>(password), std::strlen(password),
        credentials.salt.data(), credentials.salt.size(), 10000, 32, hash);
}
}
Status NvsAccessStore::init() noexcept {
    return check(nvs_open("bth_access", NVS_READWRITE, &handle_));
}
Status NvsAccessStore::load(AccessConfig& config, bool& found) noexcept {
    static_assert(std::is_trivially_copyable_v<AccessConfig>);
    static_assert(sizeof(AccessConfig) == 184, "Update settings schema when layout changes");
    std::size_t length = sizeof(config);
    const auto result = nvs_get_blob(handle_, "config_v1", &config, &length);
    found = result != ESP_ERR_NVS_NOT_FOUND;
    if (!found) { return Status::ok; }
    if (result != ESP_OK) { return check(result); }
    return length == sizeof(config) ? Status::ok : Status::invalid_argument;
}
Status NvsAccessStore::save(const AccessConfig& config) noexcept {
    auto result = nvs_set_blob(handle_, "config_v1", &config, sizeof(config));
    if (result == ESP_OK) { result = nvs_commit(handle_); }
    return check(result);
}
void NvsAccessStore::deinit() noexcept {
    if (handle_) { nvs_close(handle_); handle_ = 0; }
}
Status EspPasswordCodec::create(const char* password, WebCredentials& credentials) noexcept {
    esp_fill_random(credentials.salt.data(), credentials.salt.size());
    return derive(password, credentials, credentials.hash.data()) == 0 ? Status::ok : Status::internal_error;
}
bool EspPasswordCodec::verify(const char* password, const WebCredentials& credentials) noexcept {
    unsigned char hash[32]{};
    if (derive(password, credentials, hash) != 0) { return false; }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < sizeof(hash); ++i) { difference |= hash[i] ^ credentials.hash[i]; }
    mbedtls_platform_zeroize(hash, sizeof(hash));
    return difference == 0;
}
}  // namespace bth
