#include "bth/platform/outage_service.hpp"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstdlib>
#include <ctime>
#include <memory>
#include <new>

namespace bth {
namespace {
std::uint64_t millis() noexcept { return static_cast<std::uint64_t>(esp_timer_get_time()/1000); }
unsigned today(unsigned& minute) noexcept {
    const auto now = std::time(nullptr);
    if (now < 1704067200) return 0;
    std::tm t{};
    localtime_r(&now, &t);
    minute = static_cast<unsigned>(t.tm_hour*60+t.tm_min);
    return static_cast<unsigned>((t.tm_year+1900)*10000+(t.tm_mon+1)*100+t.tm_mday);
}
}
Status OutageService::init() noexcept {
    if (results_ || done_) return Status::invalid_state;
    results_ = xQueueCreate(1, sizeof(Snapshot));
    if (!results_) return Status::no_memory;
    done_ = xSemaphoreCreateBinary();
    if (!done_) return Status::no_memory;
    const Snapshot empty{};
    xQueueOverwrite(results_, &empty);
    return Status::ok;
}
Status OutageService::start() noexcept {
    if (!results_ || !done_ || task_) return Status::invalid_state;
    // Kyiv civil time; update this explicit rule if Ukraine changes its time policy.
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
    stopping_.store(false);
    connected_.store(false);
    return xTaskCreate(run, "outages", 8192, this, 2, &task_) == pdPASS ? Status::ok : Status::no_memory;
}
void OutageService::run(void* context) {
    auto& self = *static_cast<OutageService*>(context);
    self.work();
    xSemaphoreGive(self.done_);
    vTaskDelete(nullptr);
}
bool OutageService::fetch(unsigned group, unsigned date, OutageSchedule& result) noexcept {
    std::unique_ptr<OutageFeed> feed(new(std::nothrow) OutageFeed(group, date));
    if (!feed) return false;
    esp_http_client_config_t config{};
    config.url = "https://t.me/s/oe_cv_ua";
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 5000;
    config.disable_auto_redirect = true;
    config.buffer_size = 1024;
    config.user_agent = "B-T-HOME/0.1";
    auto client = esp_http_client_init(&config);
    if (!client) return false;
    const auto started = millis();
    bool ok = esp_http_client_open(client, 0) == ESP_OK;
    if (ok) ok = esp_http_client_fetch_headers(client) >= 0 && esp_http_client_get_status_code(client) == 200;
    char bytes[1024];
    while (ok && !esp_http_client_is_complete_data_received(client)) {
        if (stopping_.load() || !connected_.load() || settings_.group() != group || millis()-started >= 20000) { ok = false; break; }
        const int count = esp_http_client_read(client, bytes, sizeof(bytes));
        if (count <= 0) { ok = count == 0 && esp_http_client_is_complete_data_received(client); break; }
        ok = feed->feed(bytes, static_cast<std::size_t>(count));
    }
    ok = ok && esp_http_client_is_complete_data_received(client) && feed->complete();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ok) result = feed->result();
    return ok;
}
void OutageService::work() noexcept {
    OutagePoll poll;
    bool sntp = false;
    unsigned prior_date = 0;
    while (!stopping_.load()) {
        const unsigned group = settings_.group();
        const bool connected = connected_.load();
        if (connected && !sntp) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            sntp = esp_netif_sntp_init(&config) == ESP_OK;
        }
        unsigned minute = 0;
        const auto date = today(minute);
        if (date != prior_date) { poll = {}; prior_date = date; }
        if (poll.due(group, connected && date != 0, millis())) {
            Snapshot fresh{};
            const bool received = fetch(group, date, fresh.schedule);
            if (received && !stopping_.load() && connected_.load() && settings_.group() == group) {
                fresh.at = millis();
                xQueueOverwrite(results_, &fresh);
                ESP_LOGI("outages", "Group %u: %s for %u", group, fresh.schedule.valid ? "updated" : "no recognized schedule", date);
            } else ESP_LOGW("outages", "Schedule request failed; retry in 5 minutes");
        }
        if (sntp && !connected) { esp_netif_sntp_deinit(); sntp = false; }
        // Short sleeps keep mode changes and shutdown responsive between requests.
        for (unsigned i=0; i<10 && !stopping_.load(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (sntp) esp_netif_sntp_deinit();
}
OutageDisplay OutageService::display() const noexcept {
    Snapshot snapshot{};
    if (results_) xQueuePeek(results_, &snapshot, 0);
    unsigned minute = 0;
    const auto date = today(minute);
    return outage_display(snapshot.schedule, settings_.group(), date, minute,
                          connected_.load(), millis()-snapshot.at);
}
void OutageService::stop() noexcept {
    stopping_.store(true);
    connected_.store(false);
    if (task_) { xSemaphoreTake(done_, portMAX_DELAY); task_ = nullptr; }
}
void OutageService::deinit() noexcept {
    stop();
    if (results_) { vQueueDelete(results_); results_ = nullptr; }
    if (done_) { vSemaphoreDelete(done_); done_ = nullptr; }
}
}  // namespace bth
