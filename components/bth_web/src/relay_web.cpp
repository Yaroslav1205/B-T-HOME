#include "bth/web/relay_web.hpp"
#include "bth/web/request_body.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include "cJSON.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_app_desc.h"

extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");

namespace bth {
namespace {
const char* boolean(bool value) { return value ? "true" : "false"; }
void headers(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
}
esp_err_t error(httpd_req_t* request, const char* status, const char* code) {
    headers(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    char body[100]{};
    std::snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}
bool mutation_header(httpd_req_t* request) {
    char value[8]{};
    return httpd_req_get_hdr_value_str(request, "X-BTH-Request", value, sizeof(value)) == ESP_OK &&
        std::strcmp(value, "1") == 0;
}
cJSON* read_json(httpd_req_t* request) {
    if (!mutation_header(request)) {
        error(request, "403 Forbidden", "same_origin_required"); return nullptr;
    }
    char body[2048]{};
    if (request->content_len == 0 || request->content_len >= sizeof(body)) {
        error(request, "400 Bad Request", "invalid_json"); return nullptr;
    }
    std::size_t received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) { error(request, "408 Request Timeout", "timeout"); return nullptr; }
        received += static_cast<std::size_t>(count);
    }
    if (!valid_flat_json_body({body, received})) {
        error(request, "400 Bad Request", "invalid_json"); return nullptr;
    }
    auto* json = cJSON_ParseWithOpts(body, nullptr, true);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json); error(request, "400 Bad Request", "invalid_json"); return nullptr;
    }
    return json;
}
const char* field(cJSON* json, const char* name) {
    auto* value = cJSON_GetObjectItemCaseSensitive(json, name);
    return cJSON_IsString(value) ? value->valuestring : nullptr;
}
esp_err_t send_json(httpd_req_t* request, cJSON* json) {
    if (!json) { return error(request, "500 Internal Server Error", "no_memory"); }
    char* text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) { return error(request, "500 Internal Server Error", "no_memory"); }
    headers(request);
    httpd_resp_set_type(request, "application/json");
    const auto result = httpd_resp_send(request, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return result;
}
void clear_cookie(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Set-Cookie", "BTH_SESSION=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}
}
Status RelayWeb::init() noexcept {
    if (voltage_queue_) return Status::invalid_state;
    unsigned char mac[6]{};
    if (esp_read_mac(mac, wifi_ ? ESP_MAC_WIFI_SOFTAP : ESP_MAC_WIFI_STA) != ESP_OK) return Status::hardware_error;
    std::snprintf(ap_mac_, sizeof(ap_mac_), "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>(mac[0]), static_cast<unsigned>(mac[1]),
                  static_cast<unsigned>(mac[2]), static_cast<unsigned>(mac[3]),
                  static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    voltages_ = {};
    snapshot_ = {};
    voltage_queue_ = xQueueCreate(1, sizeof(Snapshot));
    if (!voltage_queue_) return Status::no_memory;
    publish();
    return ota_ ? ota_->init() : Status::ok;
}
void RelayWeb::report(const MainsReading& reading) noexcept {
    voltages_.history = reading.history;
    if (!voltage_queue_) return;
    voltages_.mains = {reading.volts, static_cast<std::uint64_t>(esp_timer_get_time() / 1000),
                      true, reading.valid, reading.calibrated};
    publish();
}
void RelayWeb::report(const DcReading& reading) noexcept {
    if (!voltage_queue_) return;
    voltages_.battery = {reading.volts, static_cast<std::uint64_t>(esp_timer_get_time() / 1000),
                        true, reading.valid, true};
    publish();
}
void RelayWeb::publish() noexcept {
    snapshot_.voltages = voltages_;
    if (voltage_queue_) xQueueOverwrite(voltage_queue_, &snapshot_);
}
void RelayWeb::report_display(const OledReadings& reading, std::uint64_t now_ms) noexcept {
    snapshot_.display = reading;
    snapshot_.display_at = now_ms;
    snapshot_.display_received = true;
    publish();
}
void RelayWeb::deinit() noexcept {
    stop();
    if (ota_) ota_->deinit();
    if (voltage_queue_) { vQueueDelete(voltage_queue_); voltage_queue_ = nullptr; }
    voltages_ = {};
}
Status RelayWeb::start() noexcept {
    if (!voltage_queue_ || server_) return Status::invalid_state;
    if (ota_) { const auto result = ota_->start(); if (result != Status::ok) return result; }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 20;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    if (httpd_start(&server_, &config) != ESP_OK) { return Status::hardware_error; }
    httpd_uri_t uri{};
    uri.user_ctx = this;
    uri.uri = "/";
    uri.method = HTTP_GET;
    uri.handler = page;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/ota"; uri.handler = get_ota;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) return Status::hardware_error;
    uri.method = HTTP_POST; uri.handler = set_ota;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) return Status::hardware_error;
    uri.uri = "/api/relay"; uri.method = HTTP_GET;
    uri.handler = get_state;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_PUT;
    uri.handler = set_state;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/relay-mode"; uri.method = HTTP_PUT; uri.handler = set_relay_mode;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) return Status::hardware_error;
    uri.uri = "/api/login"; uri.method = HTTP_POST; uri.handler = login;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/logout"; uri.handler = logout;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/settings"; uri.method = HTTP_GET; uri.handler = get_settings;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_PUT; uri.handler = set_settings;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/outages"; uri.method = HTTP_GET; uri.handler = get_outages;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_PUT; uri.handler = set_outages;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/buzzer"; uri.method = HTTP_GET; uri.handler = get_buzzer;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_PUT; uri.handler = set_buzzer;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/battery"; uri.method = HTTP_GET; uri.handler = get_battery;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_PUT; uri.handler = set_battery;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/voltages"; uri.method = HTTP_GET; uri.handler = get_voltages;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.uri = "/api/zigbee"; uri.method = HTTP_GET; uri.handler = get_zigbee;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    uri.method = HTTP_POST; uri.handler = set_zigbee;
    if (httpd_register_uri_handler(server_, &uri) != ESP_OK) { return Status::hardware_error; }
    return Status::ok;
}
void RelayWeb::stop() noexcept {
    if (server_) {
        // Joins the HTTP task before RelayService and its queues are stopped.
        (void)httpd_stop(server_);
        server_ = nullptr;
    }
    if (ota_) ota_->stop();
    sessions_ = {};
    wifi_pending_ = false;
}
esp_err_t RelayWeb::page(httpd_req_t* request) {
    headers(request);
    httpd_resp_set_hdr(request, "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; img-src data:; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, reinterpret_cast<const char*>(page_start), page_end - page_start - 1);
}
esp_err_t RelayWeb::send_state(httpd_req_t* request, const RelayState& state) {
    const auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    headers(request);
    httpd_resp_set_type(request, "application/json");
    char json[320]{};
    const int length = std::snprintf(json, sizeof(json),
        "{\"mode\":%u,\"ready\":%s,\"on\":%s,\"saved_on\":%s,\"output_known\":%s,"
        "\"storage_known\":%s,\"error\":\"%s\",\"mac\":\"%s\",\"wifi_mode\":\"%s\"}",
        static_cast<unsigned>(state.mode), boolean(state.ready), boolean(state.on), boolean(state.saved_on),
        boolean(state.output_known), boolean(state.storage_known), status_name(state.error), self.ap_mac_,
        self.wifi_ ? "ap" : "matter");
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(json)) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "response_error");
    }
    return httpd_resp_send(request, json, length);
}
esp_err_t RelayWeb::get_zigbee(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    const auto state = self.zigbee_ ? self.zigbee_->snapshot() : ZigbeeSnapshot{};
    auto* json = cJSON_CreateObject();
    if (!json || !cJSON_AddStringToObject(json, "state", zigbee_state_name(state.state)) ||
        !cJSON_AddNumberToObject(json, "channel", state.channel) ||
        !cJSON_AddNumberToObject(json, "seconds_left", state.seconds_left)) {
        cJSON_Delete(json); return error(request, "500 Internal Server Error", "no_memory");
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_zigbee(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const char* action = field(json, "action");
    const bool join = action && std::strcmp(action, "join") == 0;
    const bool reset = action && std::strcmp(action, "reset") == 0;
    cJSON_Delete(json);
    if (!join && !reset) return error(request, "400 Bad Request", "invalid_action");
    if (!self.zigbee_) return error(request, "409 Conflict", "zigbee_disabled");
    if (!self.zigbee_->request(join ? ZigbeeCommand::join : ZigbeeCommand::reset))
        return error(request, "503 Service Unavailable", "zigbee_busy");
    headers(request);
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}
esp_err_t RelayWeb::get_state(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) { return ESP_OK; }
    return send_state(request, self.relay_.snapshot());
}
esp_err_t RelayWeb::get_voltages(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    Snapshot queued{};
    if (!self.voltage_queue_ || xQueuePeek(self.voltage_queue_, &queued, 0) != pdTRUE)
        return error(request, "503 Service Unavailable", "measurements_unavailable");
    const auto& snapshot = queued.voltages;
    const auto now = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    auto* json = cJSON_CreateObject();
    if (!json) return error(request, "500 Internal Server Error", "no_memory");
    const auto add = [&](const char* name, const WebVoltage& value) {
        auto* item = cJSON_AddObjectToObject(json, name);
        if (!item) return false;
        const auto* status = voltage_status(value, now);
        return cJSON_AddStringToObject(item, "status", status) &&
            (std::strcmp(status, "ok") == 0 ? cJSON_AddNumberToObject(item, "volts", value.volts)
                                           : cJSON_AddNullToObject(item, "volts")) &&
            (value.received && now >= value.updated_ms
                ? cJSON_AddNumberToObject(item, "age_ms", static_cast<double>(now - value.updated_ms))
                : cJSON_AddNullToObject(item, "age_ms"));
    };
    const auto add_period = [&](const char* name, const MainsPeriod& period) {
        if (!period.known || std::strcmp(voltage_status(snapshot.mains, now), "ok") != 0)
            return cJSON_AddNullToObject(json, name) != nullptr;
        auto* item = cJSON_AddObjectToObject(json, name);
        return item && cJSON_AddBoolToObject(item, "present", period.present) &&
            cJSON_AddNumberToObject(item, "duration_ms", static_cast<double>(period.duration_ms));
    };
    if (!add("mains", snapshot.mains) || !add("battery", snapshot.battery) ||
        !add_period("current_period", snapshot.history.current) ||
        !add_period("previous_period", snapshot.history.previous)) {
        cJSON_Delete(json);
        return error(request, "500 Internal Server Error", "no_memory");
    }
    if (queued.display_received && now >= queued.display_at && now - queued.display_at < 2500) {
        const auto& d = queued.display;
        auto* display = cJSON_AddObjectToObject(json, "display");
        const char* outage_states[] = {"hidden", "unavailable", "stale", "clear", "interval"};
        char ip[16]{};
        if (d.ipv4 != std::array<std::uint8_t, 4>{})
            std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                unsigned(d.ipv4[0]), unsigned(d.ipv4[1]), unsigned(d.ipv4[2]), unsigned(d.ipv4[3]));
        const bool remaining = d.battery.valid && d.battery.mains_known && !d.battery.mains_present;
        if (!display ||
            !cJSON_AddNumberToObject(display, "age_ms", static_cast<double>(now - queued.display_at)) ||
            !(d.average_valid ? cJSON_AddNumberToObject(display, "average_volts", d.average_volts)
                              : cJSON_AddNullToObject(display, "average_volts")) ||
            !(remaining ? cJSON_AddNumberToObject(display, "battery_minutes", static_cast<double>(d.battery.minutes))
                        : cJSON_AddNullToObject(display, "battery_minutes")) ||
            !cJSON_AddBoolToObject(display, "mains_present", d.battery.mains_known && d.battery.mains_present) ||
            !(d.relay_valid ? cJSON_AddBoolToObject(display, "relay_on", d.relay_on)
                           : cJSON_AddNullToObject(display, "relay_on")) ||
            !cJSON_AddStringToObject(display, "ipv4", ip) ||
            !cJSON_AddStringToObject(display, "outage_state", outage_states[static_cast<unsigned>(d.outages.state)]) ||
            !cJSON_AddNumberToObject(display, "outage_group", d.outages.group) ||
            !cJSON_AddNumberToObject(display, "outage_begin", d.outages.begin) ||
            !cJSON_AddNumberToObject(display, "outage_end", d.outages.end)) {
            cJSON_Delete(json);
            return error(request, "500 Internal Server Error", "no_memory");
        }
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_state(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) { return ESP_OK; }
    // No CORS: the custom header and PUT prevent cross-origin form requests.
    char header[8]{};
    if (httpd_req_get_hdr_value_str(request, "X-BTH-Request", header, sizeof(header)) != ESP_OK ||
        std::strcmp(header, "1") != 0) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "same_origin_required");
    }
    if (request->content_len != 4 && request->content_len != 5) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "expected_boolean");
    }
    char body[6]{};
    std::size_t received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) { return ESP_FAIL; }
        received += static_cast<std::size_t>(count);
    }
    if (self.relay_.snapshot().mode != RelayMode::manual)
        return error(request, "409 Conflict", "manual_mode_required");
    bool on = false;
    if (!parse_relay_command({body, received}, on)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "expected_boolean");
    }
    RelayState state{};
    if (!self.relay_.request(on, state)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_send(request, "command_pending", HTTPD_RESP_USE_STRLEN);
    }
    if (!state.ready || state.error != Status::ok) {
        httpd_resp_set_status(request, "503 Service Unavailable");
    }
    return send_state(request, state);
}
RelayWeb::Session* RelayWeb::find_session(httpd_req_t* request) noexcept {
    char cookie[256]{};
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) { return nullptr; }
    const auto now = esp_timer_get_time();
    for (char* item = cookie; item && *item;) {
        while (*item == ' ') { ++item; }
        char* end = std::strchr(item, ';');
        const auto length = end ? static_cast<std::size_t>(end - item) : std::strlen(item);
        if (length == 44 && std::strncmp(item, "BTH_SESSION=", 12) == 0) {
            for (auto& session : sessions_) {
                unsigned char difference = 0;
                for (std::size_t i = 0; i < 32; ++i) { difference |= session.token[i] ^ item[12 + i]; }
                if (session.expires > now && difference == 0) { return &session; }
            }
        }
        item = end ? end + 1 : nullptr;
    }
    return nullptr;
}
bool RelayWeb::authorized(httpd_req_t* request) noexcept {
    auto* session = settings_.usable() ? find_session(request) : nullptr;
    if (!session) {
        clear_cookie(request);
        error(request, "401 Unauthorized", "login_required");
        return false;
    }
    return true;
}
esp_err_t RelayWeb::login(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    const auto now = esp_timer_get_time();
    if (now - self.login_window_ >= 30000000) { self.login_failures_ = 0; self.login_window_ = now; }
    if (self.login_failures_ >= 5) { return error(request, "429 Too Many Requests", "try_later"); }
    auto* json = read_json(request);
    if (!json) { return ESP_OK; }
    const bool valid = self.settings_.authenticate(field(json, "username"), field(json, "password"));
    cJSON_Delete(json);
    if (!valid) {
        ++self.login_failures_;
        return error(request, "401 Unauthorized", "invalid_credentials");
    }
    self.login_failures_ = 0;
    auto* selected = self.find_session(request);
    if (!selected) {
        selected = &self.sessions_[0];
        for (auto& session : self.sessions_) {
            if (session.expires < selected->expires) { selected = &session; }
        }
    }
    unsigned char random[16]{};
    esp_fill_random(random, sizeof(random));
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < sizeof(random); ++i) {
        selected->token[2 * i] = hex[random[i] >> 4];
        selected->token[2 * i + 1] = hex[random[i] & 15];
    }
    selected->token[32] = 0;
    selected->expires = now + 3600000000LL;
    char cookie[120]{};
    std::snprintf(cookie, sizeof(cookie), "BTH_SESSION=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600", selected->token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}
esp_err_t RelayWeb::logout(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!mutation_header(request)) { return error(request, "403 Forbidden", "same_origin_required"); }
    if (auto* session = self.find_session(request)) { *session = {}; }
    clear_cookie(request);
    headers(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}
esp_err_t RelayWeb::get_settings(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) { return ESP_OK; }
    const auto& config = self.settings_.config();
    auto* json = cJSON_CreateObject();
    if (json) {
        cJSON_AddStringToObject(json, "ssid", config.wifi.ssid);
        cJSON_AddStringToObject(json, "username", config.web.username);
        cJSON_AddStringToObject(json, "primary_ssid", config.stations[0].ssid);
        cJSON_AddStringToObject(json, "backup_ssid", config.stations[1].ssid);
        cJSON_AddBoolToObject(json, "primary_open", config.stations[0].ssid[0] && !config.stations[0].password[0]);
        cJSON_AddBoolToObject(json, "backup_open", config.stations[1].ssid[0] && !config.stations[1].password[0]);
        cJSON_AddBoolToObject(json, "wifi_pending", self.wifi_ && !self.wifi_->station_mode() && !same_wifi(config.wifi, self.wifi_->active()));
        cJSON_AddBoolToObject(json, "wifi_managed_by_matter", self.wifi_ == nullptr);
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_settings(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) { return ESP_OK; }
    if (self.ota_ && (self.ota_->busy() || self.ota_->reboot_requested())) return error(request, "409 Conflict", "ota_busy");
    if (self.wifi_pending_) { return error(request, "409 Conflict", "wifi_pending"); }
    auto* json = read_json(request);
    if (!json) { return ESP_OK; }
    const bool access_fields = cJSON_HasObjectItem(json, "ssid") || cJSON_HasObjectItem(json, "wifi_password") ||
        cJSON_HasObjectItem(json, "username") || cJSON_HasObjectItem(json, "web_password");
    const bool router_fields = cJSON_HasObjectItem(json, "primary_ssid") || cJSON_HasObjectItem(json, "primary_password") ||
        cJSON_HasObjectItem(json, "primary_open") || cJSON_HasObjectItem(json, "backup_ssid") ||
        cJSON_HasObjectItem(json, "backup_password") || cJSON_HasObjectItem(json, "backup_open");
    if (!access_fields && !router_fields) {
        cJSON_Delete(json); return error(request, "400 Bad Request", "invalid_settings");
    }
    const char* ssid = access_fields ? field(json, "ssid") : self.settings_.config().wifi.ssid;
    const char* password = access_fields ? field(json, "wifi_password") : "";
    const char* username = access_fields ? field(json, "username") : self.settings_.config().web.username;
    const char* web_password = access_fields ? field(json, "web_password") : "";
    if (!ssid || !password || !username || !web_password || std::strlen(ssid) > 32 || std::strlen(password) > 63) {
        cJSON_Delete(json); return error(request, "400 Bad Request", "invalid_settings");
    }
    WifiSettings wifi = self.settings_.config().wifi;
    std::strcpy(wifi.ssid, ssid);
    if (password[0]) { std::strcpy(wifi.password, password); }
    if (!self.wifi_ && !same_wifi(wifi, self.settings_.config().wifi)) {
        cJSON_Delete(json); return error(request, "409 Conflict", "wifi_managed_by_matter");
    }
    auto stations = self.settings_.config().stations;
    const char* names[] = {"primary_ssid", "backup_ssid"};
    const char* passwords[] = {"primary_password", "backup_password"};
    const char* open_fields[] = {"primary_open", "backup_open"};
    for (unsigned i = 0; i < 2; ++i) {
        const auto* name = field(json, names[i]);
        const auto* secret = field(json, passwords[i]);
        const auto* open = cJSON_GetObjectItemCaseSensitive(json, open_fields[i]);
        // Omitted fields preserve settings for existing API clients.
        if (!cJSON_HasObjectItem(json, names[i]) && !cJSON_HasObjectItem(json, passwords[i]) && !open) continue;
        if (!self.wifi_ || !name || !secret || std::strlen(name) > 32 || std::strlen(secret) > 63 ||
            !cJSON_IsBool(open)) {
            cJSON_Delete(json); return error(request, "400 Bad Request", "invalid_settings");
        }
        const bool renamed = std::strcmp(stations[i].ssid, name) != 0;
        if (renamed) stations[i] = {};
        std::strcpy(stations[i].ssid, name);
        if (!name[0] || cJSON_IsTrue(open)) stations[i].password[0] = 0;
        else if (secret[0]) std::strcpy(stations[i].password, secret);
        else if (!stations[i].password[0]) {
            cJSON_Delete(json); return error(request, "400 Bad Request", "invalid_settings");
        }
    }
    const bool wifi_changed = access_fields && self.wifi_ && !self.wifi_->station_mode() && !same_wifi(wifi, self.wifi_->active());
    bool credentials_changed = false;
    const auto result = self.settings_.update(wifi, username, web_password, credentials_changed, &stations);
    cJSON_Delete(json);
    if (result != Status::ok) {
        if (result == Status::invalid_argument) { return error(request, "400 Bad Request", "invalid_settings"); }
        self.sessions_ = {}; clear_cookie(request);
        return error(request, "503 Service Unavailable", "save_failed");
    }
    if (credentials_changed) { self.sessions_ = {}; clear_cookie(request); }
    bool queued = false;
    if (wifi_changed) {
        self.wifi_pending_ = true;
        queued = httpd_queue_work(self.server_, apply_wifi, &self) == ESP_OK;
        if (!queued) { self.wifi_pending_ = false; }
    }
    auto* response = cJSON_CreateObject();
    if (response) {
        cJSON_AddBoolToObject(response, "ok", true);
        cJSON_AddBoolToObject(response, "reconnect", wifi_changed && queued);
        cJSON_AddBoolToObject(response, "restart_required", wifi_changed && !queued);
        cJSON_AddBoolToObject(response, "login_required", credentials_changed);
        cJSON_AddStringToObject(response, "ssid", wifi.ssid);
    }
    return send_json(request, response);
}
esp_err_t RelayWeb::set_relay_mode(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const auto* value = cJSON_GetObjectItemCaseSensitive(json, "mode");
    const bool valid = cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
        value->valuedouble >= 0 && value->valuedouble <= 2 &&
        std::floor(value->valuedouble) == value->valuedouble;
    if (!valid) {
        cJSON_Delete(json);
        return error(request, "400 Bad Request", "invalid_mode");
    }
    const auto mode = static_cast<RelayMode>(value->valueint);
    cJSON_Delete(json);
    RelayState state{};
    if (!self.relay_.request_mode(mode, state))
        return error(request, "503 Service Unavailable", "command_pending");
    if (!state.ready || state.error != Status::ok || state.mode != mode)
        httpd_resp_set_status(request, "503 Service Unavailable");
    return send_state(request, state);
}
esp_err_t RelayWeb::get_ota(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = cJSON_CreateObject();
    if (json) {
        const auto value = self.ota_ ? self.ota_->snapshot() : OtaSnapshot{};
        cJSON_AddBoolToObject(json, "supported", self.ota_ && OtaService::supported());
        cJSON_AddBoolToObject(json, "connected", self.ota_ && self.ota_->connected());
        cJSON_AddBoolToObject(json, "busy", self.ota_ && self.ota_->busy());
        cJSON_AddStringToObject(json, "current_version", esp_app_get_description()->version);
        cJSON_AddStringToObject(json, "version", value.version);
        cJSON_AddStringToObject(json, "state", value.state);
        cJSON_AddStringToObject(json, "error", value.error);
        cJSON_AddNumberToObject(json, "received", value.received);
        cJSON_AddNumberToObject(json, "total", value.total);
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_ota(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const char* action = field(json, "action");
    const bool install = action && std::strcmp(action, "install") == 0;
    const bool check = action && std::strcmp(action, "check") == 0;
    cJSON_Delete(json);
    if (!install && !check) return error(request, "400 Bad Request", "invalid_action");
    if (!self.ota_ || !OtaService::supported()) return error(request, "409 Conflict", "ota_unsupported");
    if (self.wifi_pending_) return error(request, "409 Conflict", "wifi_pending");
    if (!self.ota_->connected()) return error(request, "409 Conflict", "wifi_required");
    if (!self.ota_->request(install)) return error(request, "409 Conflict", "ota_busy");
    httpd_resp_set_status(request, "202 Accepted");
    return get_ota(request);
}
esp_err_t RelayWeb::get_outages(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = cJSON_CreateObject();
    if (json) {
        cJSON_AddBoolToObject(json, "available", self.outages_ != nullptr);
        if (self.outages_) cJSON_AddNumberToObject(json, "group", self.outages_->group());
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_outages(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    if (!self.outages_) return error(request, "404 Not Found", "outages_unavailable");
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const auto* value = cJSON_GetObjectItemCaseSensitive(json, "group");
    const bool valid = cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
        value->valuedouble >= 0 && value->valuedouble <= outage_max_group &&
        std::floor(value->valuedouble) == value->valuedouble;
    if (!valid) {
        cJSON_Delete(json);
        return error(request, "400 Bad Request", "invalid_group");
    }
    const auto group = static_cast<std::uint32_t>(value->valuedouble);
    cJSON_Delete(json);
    const auto result = self.outages_->update_group(group);
    if (result != Status::ok) return error(request, "503 Service Unavailable", "save_failed");
    return get_outages(request);
}
esp_err_t RelayWeb::get_buzzer(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = cJSON_CreateObject();
    if (json) {
        cJSON_AddBoolToObject(json, "available", self.buzzer_ != nullptr);
        if (self.buzzer_) cJSON_AddNumberToObject(json, "interval_seconds", self.buzzer_->interval_seconds());
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_buzzer(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    if (!self.buzzer_) return error(request, "404 Not Found", "buzzer_unavailable");
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const auto* value = cJSON_GetObjectItemCaseSensitive(json, "interval_seconds");
    const bool valid = cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
        value->valuedouble >= 0 && value->valuedouble <= buzzer_max_interval_seconds &&
        std::floor(value->valuedouble) == value->valuedouble;
    if (!valid) {
        cJSON_Delete(json);
        return error(request, "400 Bad Request", "invalid_interval");
    }
    const auto seconds = static_cast<std::uint32_t>(value->valuedouble);
    cJSON_Delete(json);
    const auto result = self.buzzer_->update_interval(seconds);
    if (result != Status::ok) return error(request, "503 Service Unavailable", "save_failed");
    return get_buzzer(request);
}
esp_err_t RelayWeb::get_battery(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    auto* json = cJSON_CreateObject();
    if (json) {
        cJSON_AddBoolToObject(json, "available", self.battery_ != nullptr);
        if (self.battery_) cJSON_AddNumberToObject(json, "discharge_ma", self.battery_->discharge_ma());
    }
    return send_json(request, json);
}
esp_err_t RelayWeb::set_battery(httpd_req_t* request) {
    auto& self = *static_cast<RelayWeb*>(request->user_ctx);
    if (!self.authorized(request)) return ESP_OK;
    if (!self.battery_) return error(request, "404 Not Found", "battery_unavailable");
    auto* json = read_json(request);
    if (!json) return ESP_OK;
    const auto* value = cJSON_GetObjectItemCaseSensitive(json, "discharge_ma");
    const bool valid = cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
        value->valuedouble >= 1 && value->valuedouble <= battery_max_discharge_ma &&
        std::floor(value->valuedouble) == value->valuedouble;
    if (!valid) {
        cJSON_Delete(json);
        return error(request, "400 Bad Request", "invalid_interval");
    }
    const auto milliamps = static_cast<std::uint32_t>(value->valuedouble);
    cJSON_Delete(json);
    const auto result = self.battery_->update_discharge(milliamps);
    if (result != Status::ok) return error(request, "503 Service Unavailable", "save_failed");
    return get_battery(request);
}
void RelayWeb::apply_wifi(void* context) {
    auto& self = *static_cast<RelayWeb*>(context);
    // Runs on the HTTP owner after the response. No Runtime calls or detached task.
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!self.wifi_ || self.wifi_->apply(self.settings_.config().wifi) != Status::ok) {
        ESP_LOGE("web", "Wi-Fi settings saved, but live apply failed");
    }
    self.wifi_pending_ = false;
}
}  // namespace bth
