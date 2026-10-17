#pragma once
#include <array>
#include "bth/core/module.hpp"
#include "bth/platform/relay_service.hpp"
#include "bth/platform/wifi_ap.hpp"
#include "bth/access/access.hpp"
#include "esp_http_server.h"
#include "bth/mains/mains.hpp"
#include "bth/dc/dc.hpp"
#include "bth/web/voltage.hpp"
#include "freertos/queue.h"
#include "bth/zigbee/control.hpp"
#include "bth/buzzer/settings.hpp"
namespace bth {
class RelayWeb final : public Module, public MainsSink, public DcSink {
public:
    RelayWeb(RelayService& relay, AccessSettings& settings, WifiAp& wifi,
             ZigbeeControl* zigbee = nullptr, BuzzerSettings* buzzer = nullptr) noexcept
        : relay_(relay), settings_(settings), wifi_(&wifi), zigbee_(zigbee), buzzer_(buzzer) {}
    // Matter owns Wi-Fi provisioning; this web server only edits web credentials.
    RelayWeb(RelayService& relay, AccessSettings& settings, BuzzerSettings* buzzer = nullptr) noexcept
        : relay_(relay), settings_(settings), buzzer_(buzzer) {}
    const char* id() const noexcept override { return "web"; }
    Dependencies dependencies() const noexcept override {
        return wifi_ ? Dependencies{required_, zigbee_ ? 4U : 3U} : Dependencies{matter_required_, 3};
    }
    Status init() noexcept override;
    // Runtime task only; HTTP consumes a copied snapshot through the queue.
    void report(const MainsReading& reading) noexcept override;
    void report(const DcReading& reading) noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    struct Session { char token[33]{}; std::int64_t expires = 0; };
    inline static constexpr const char* required_[] = {"wifi", "relay", "access", "zigbee"};
    inline static constexpr const char* matter_required_[] = {"matter", "relay", "access"};
    static esp_err_t get_zigbee(httpd_req_t* request);
    static esp_err_t set_zigbee(httpd_req_t* request);
    static esp_err_t page(httpd_req_t* request);
    static esp_err_t get_state(httpd_req_t* request);
    static esp_err_t get_voltages(httpd_req_t* request);
    static esp_err_t set_state(httpd_req_t* request);
    static esp_err_t send_state(httpd_req_t* request, const RelayState& state);
    static esp_err_t login(httpd_req_t* request);
    static esp_err_t logout(httpd_req_t* request);
    static esp_err_t get_settings(httpd_req_t* request);
    static esp_err_t set_settings(httpd_req_t* request);
    static esp_err_t get_buzzer(httpd_req_t* request);
    static esp_err_t set_buzzer(httpd_req_t* request);
    static void apply_wifi(void* context);
    Session* find_session(httpd_req_t* request) noexcept;
    bool authorized(httpd_req_t* request) noexcept;
    RelayService& relay_;
    AccessSettings& settings_;
    WifiAp* wifi_ = nullptr;
    ZigbeeControl* zigbee_ = nullptr;
    BuzzerSettings* buzzer_ = nullptr;
    httpd_handle_t server_ = nullptr;
    char ap_mac_[18]{};
    std::array<Session, 4> sessions_{};
    std::int64_t login_window_ = 0;
    unsigned login_failures_ = 0;
    bool wifi_pending_ = false;
    WebVoltages voltages_{};
    QueueHandle_t voltage_queue_ = nullptr;
};
}  // namespace bth
