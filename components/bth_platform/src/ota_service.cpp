#include "bth/platform/ota_service.hpp"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

namespace bth {
namespace {
constexpr const char* release_root = "https://github.com/Yaroslav1205/B-T-HOME/releases/";
bool allowed_url(const char* url) noexcept {
    const char* prefixes[] = {release_root, "https://release-assets.githubusercontent.com/",
                             "https://objects.githubusercontent.com/"};
    for (const auto* prefix : prefixes)
        if (std::strncmp(url, prefix, std::strlen(prefix)) == 0) return true;
    return false;
}
class Download final : public ota::Source {
public:
    ~Download() override {
        if (client_) { esp_http_client_close(client_); esp_http_client_cleanup(client_); }
    }
    bool open(const char* url) noexcept {
        if (!allowed_url(url)) return false;
        esp_http_client_config_t config{};
        config.url = url;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.timeout_ms = 5000;
        config.disable_auto_redirect = true;
        config.buffer_size = 2048;
        config.buffer_size_tx = 2048;
        config.user_agent = "B-T-HOME-OTA";
        client_ = esp_http_client_init(&config);
        if (!client_) return false;
        for (unsigned redirects = 0; redirects < 5; ++redirects) {
            if (esp_http_client_open(client_, 0) != ESP_OK || esp_http_client_fetch_headers(client_) < 0) return false;
            const auto status = esp_http_client_get_status_code(client_);
            if (status == 200) return true;
            if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) return false;
            if (esp_http_client_set_redirection(client_) != ESP_OK) return false;
            char next[2048]{};
            if (esp_http_client_get_url(client_, next, sizeof(next)) != ESP_OK || !allowed_url(next)) return false;
            esp_http_client_close(client_);
        }
        return false;
    }
    int read(void* buffer, std::size_t capacity) noexcept override {
        const auto count = esp_http_client_read(client_, static_cast<char*>(buffer), static_cast<int>(capacity));
        if (count == 0 && !esp_http_client_is_complete_data_received(client_)) return -1;
        return count;
    }
private:
    esp_http_client_handle_t client_ = nullptr;
};
class Flash final : public ota::Target {
public:
    Flash() noexcept { mbedtls_sha256_init(&hash_); }
    ~Flash() override { abort(); mbedtls_sha256_free(&hash_); }
    bool begin(std::size_t size) noexcept override {
        partition_ = esp_ota_get_next_update_partition(nullptr);
        return partition_ && partition_ != esp_ota_get_running_partition() && size <= partition_->size &&
            mbedtls_sha256_starts(&hash_, 0) == 0 &&
            esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) == ESP_OK;
    }
    bool write(const void* bytes, std::size_t size) noexcept override {
        return mbedtls_sha256_update(&hash_, static_cast<const unsigned char*>(bytes), size) == 0 &&
            esp_ota_write(handle_, bytes, size) == ESP_OK;
    }
    bool checksum(const char* expected) noexcept override {
        unsigned char digest[32]{};
        if (mbedtls_sha256_finish(&hash_, digest) != 0) return false;
        char hex[65]{};
        for (unsigned i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        return std::strcmp(hex, expected) == 0;
    }
    bool finish() noexcept override {
        const auto handle = handle_;
        handle_ = 0; // esp_ota_end releases the handle even on validation failure.
        return esp_ota_end(handle) == ESP_OK;
    }
    bool activate() noexcept override { return esp_ota_set_boot_partition(partition_) == ESP_OK; }
    void abort() noexcept override { if (handle_) { esp_ota_abort(handle_); handle_ = 0; } }
private:
    esp_ota_handle_t handle_ = 0;
    const esp_partition_t* partition_ = nullptr;
    mbedtls_sha256_context hash_{};
};
const char* string(cJSON* json, const char* key) noexcept {
    auto* item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}
bool equals(cJSON* json, const char* key, const char* value) noexcept {
    return std::strcmp(string(json, key), value) == 0;
}
template<std::size_t N> bool copy(char (&to)[N], const char* from) noexcept {
    if (std::strlen(from) >= N) return false;
    std::strcpy(to, from);
    return true;
}
}
bool OtaService::supported() noexcept {
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    const auto* running = esp_ota_get_running_partition();
    const auto* next = esp_ota_get_next_update_partition(nullptr);
    const auto* data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    return running && next && data && running != next && running->size == ota::slot_size &&
        next->size == ota::slot_size && data->address == 0x1f0000 &&
        ((running->address == 0x10000 && next->address == 0x220000) ||
         (running->address == 0x220000 && next->address == 0x10000));
#else
    return false;
#endif
}
bool OtaService::confirm_boot() noexcept {
    esp_ota_img_states_t state{};
    const auto* running = esp_ota_get_running_partition();
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return true;
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}
Status OtaService::init() noexcept {
    if (commands_ || snapshots_ || done_) return Status::invalid_state;
    commands_ = xQueueCreate(1, sizeof(bool));
    snapshots_ = xQueueCreate(1, sizeof(OtaSnapshot));
    done_ = xSemaphoreCreateBinary();
    if (!commands_ || !snapshots_ || !done_) return Status::no_memory;
    current_ = {};
    xQueueOverwrite(snapshots_, &current_);
    return Status::ok;
}
Status OtaService::start() noexcept {
    if (!commands_ || !snapshots_ || !done_ || task_) return Status::invalid_state;
    stopping_.store(false);
    connected_.store(false);
    busy_.store(false);
    xQueueReset(commands_);
    return xTaskCreate(run, "ota", 12288, this, 2, &task_) == pdPASS ? Status::ok : Status::no_memory;
}
void OtaService::stop() noexcept {
    stopping_.store(true);
    connected_.store(false);
    if (task_) { xSemaphoreTake(done_, portMAX_DELAY); task_ = nullptr; }
    busy_.store(false);
}
void OtaService::deinit() noexcept {
    stop();
    if (commands_) { vQueueDelete(commands_); commands_ = nullptr; }
    if (snapshots_) { vQueueDelete(snapshots_); snapshots_ = nullptr; }
    if (done_) { vSemaphoreDelete(done_); done_ = nullptr; }
}
bool OtaService::request(bool install) noexcept {
    if (stopping_.load() || !connected_.load() || reboot_.load() || !supported()) return false;
    bool idle = false;
    if (!busy_.compare_exchange_strong(idle, true)) return false;
    if (xQueueSend(commands_, &install, 0) == pdTRUE) return true;
    busy_.store(false);
    return false;
}
OtaSnapshot OtaService::snapshot() const noexcept {
    OtaSnapshot value{};
    if (snapshots_) xQueuePeek(snapshots_, &value, 0);
    return value;
}
void OtaService::publish(const char* state, const char* error) noexcept {
    copy(current_.state, state);
    copy(current_.error, error);
    xQueueOverwrite(snapshots_, &current_);
}
bool OtaService::cancelled() const noexcept {
    return stopping_.load() || !connected_.load() || esp_timer_get_time() > deadline_;
}
void OtaService::progress(std::size_t bytes) noexcept {
    current_.received = bytes;
    publish("downloading");
}
bool OtaService::check(ota::Release& release) noexcept {
    Download download;
    if (!download.open("https://github.com/Yaroslav1205/B-T-HOME/releases/latest/download/manifest.json")) return false;
    char body[2048]{};
    std::size_t size = 0;
    for (;;) {
        if (cancelled() || size == sizeof(body) - 1) return false;
        const int count = download.read(body + size, sizeof(body) - 1 - size);
        if (count < 0) return false;
        if (!count) break;
        size += static_cast<std::size_t>(count);
    }
    // No duplicate fields: ambiguous security metadata is rejected.
    if (std::memchr(body, 0, size)) return false;
    auto* json = cJSON_ParseWithOpts(body, nullptr, true);
    bool unique = cJSON_IsObject(json);
    for (auto* a = json ? json->child : nullptr; a; a = a->next)
        for (auto* b = a->next; b; b = b->next)
            if (a->string && b->string && std::strcmp(a->string, b->string) == 0) unique = false;
    const auto* schema = cJSON_GetObjectItemCaseSensitive(json, "schema_version");
    const auto* length = cJSON_GetObjectItemCaseSensitive(json, "size");
    bool ok = unique && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "ota_ready")) &&
        equals(json, "repository", "Yaroslav1205/B-T-HOME") && equals(json, "project", "bt_home") &&
        equals(json, "board", "esp32c6_rgb8") && equals(json, "target", "esp32c6") &&
        equals(json, "variant", "zigbee-wifi") && equals(json, "layout", ota::layout) &&
        cJSON_IsNumber(length) && length->valuedouble >= 288 && length->valuedouble <= ota::slot_size &&
        std::floor(length->valuedouble) == length->valuedouble &&
        copy(release.version, string(json, "version")) && copy(release.file, string(json, "file")) &&
        copy(release.sha256, string(json, "sha256"));
    if (ok) release.size = static_cast<std::size_t>(length->valuedouble);
    cJSON_Delete(json);
    return ok && ota::valid(release);
}
void OtaService::run(void* context) {
    auto& self = *static_cast<OtaService*>(context);
    self.work();
    xSemaphoreGive(self.done_);
    vTaskDelete(nullptr);
}
void OtaService::work() noexcept {
    while (!stopping_.load()) {
        bool install = false;
        if (xQueueReceive(commands_, &install, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        current_ = {};
        deadline_ = esp_timer_get_time() + 180LL * 1000000;
        publish("checking");
        ota::Release release{};
        if (std::time(nullptr) < 1704067200) {
            publish("error", "clock_not_ready");
        } else if (!check(release) || cancelled()) {
            publish("error", cancelled() ? "cancelled" : "release_unavailable");
        } else {
            copy(current_.version, release.version);
            current_.total = release.size;
            if (!ota::newer(release.version, esp_app_get_description()->version)) publish("up_to_date");
            else if (!install) publish("available");
            else {
                Download download;
                Flash flash;
                char url[256]{};
                std::snprintf(url, sizeof(url), "%sdownload/v%s/%s", release_root, release.version, release.file);
                publish("downloading");
                const auto result = download.open(url) ? ota::install(release, download, flash, *this) : ota::Result::download;
                if (result == ota::Result::ok) {
                    publish("restarting");
                    reboot_.store(true);
                } else publish("error", ota::name(result));
            }
        }
        busy_.store(false);
    }
}
}  // namespace bth
