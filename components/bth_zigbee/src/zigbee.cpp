#include "bth/zigbee/zigbee.hpp"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

namespace bth {
namespace {
constexpr const char* tag = "zigbee";
constexpr std::uint8_t endpoint = 1;
std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time() / 1000); }
const char* commissioning_status(unsigned status) {
    switch (status) {
    case EZB_BDB_STATUS_SUCCESS: return "success";
    case EZB_BDB_STATUS_IN_PROGRESS: return "in_progress";
    case EZB_BDB_STATUS_NO_NETWORK: return "no_network";
    case EZB_BDB_STATUS_TCLK_EX_FAILURE: return "trust_center_key_exchange_failed";
    case EZB_BDB_STATUS_NOT_ON_A_NETWORK: return "not_on_network";
    case EZB_BDB_STATUS_ON_A_NETWORK: return "already_on_network";
    case EZB_BDB_STATUS_CANCELLED: return "cancelled";
    case EZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE: return "device_announce_failed";
    default: return "other_or_missing";
    }
}
}
ZigbeeModule* ZigbeeModule::owner_ = nullptr;

Status ZigbeeModule::init() noexcept {
    if (owner_ || initialized_ || commands_) return Status::invalid_state;
    if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "zb_storage")) {
        ESP_LOGE(tag, "Missing zb_storage NVS partition; flash the updated partition table");
        return Status::invalid_argument;
    }
    commands_ = xQueueCreate(2, sizeof(ZigbeeCommand));
    states_ = xQueueCreate(1, sizeof(ZigbeeSnapshot));
    joined_ = xSemaphoreCreateBinary();
    if (!commands_ || !states_ || !joined_) return Status::no_memory;
    const auto storage_result = storage_.init();
    if (storage_result != Status::ok) return storage_result;
    esp_zigbee_config_t config{};
    config.device_config.device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE;
    config.device_config.zed_config.ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN;
    config.device_config.zed_config.keep_alive = 3000;
    config.platform_config.storage_partition_name = "zb_storage";
    config.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE;
    const auto result = esp_zigbee_init(&config);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "Init: %s", esp_err_to_name(result));
        return Status::hardware_error;
    }
    initialized_ = true;
    owner_ = this;
    const ZigbeeSnapshot initial{ZigbeeState::starting, 0, 0};
    xQueueOverwrite(states_, &initial);
    return Status::ok;
}

bool ZigbeeModule::setup_endpoint() noexcept {
    descriptor_ = ezb_af_create_device_desc();
    if (!descriptor_) return false;
    ezb_af_ep_config_t config{};
    config.ep_id = endpoint;
    config.app_profile_id = EZB_AF_HA_PROFILE_ID;
    config.app_device_id = EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID;
    const auto ep = ezb_af_create_endpoint_desc(&config);
    if (!ep) return false;
    if (ezb_af_device_add_endpoint_desc(descriptor_, ep) != EZB_ERR_NONE) {
        ezb_af_free_endpoint_desc(ep); return false;
    }
    const auto add = [&](ezb_zcl_cluster_desc_t cluster) {
        if (!cluster) return false;
        if (ezb_af_endpoint_add_cluster_desc(ep, cluster) == EZB_ERR_NONE) return true;
        ezb_zcl_free_cluster_desc(cluster); return false;
    };
    ezb_zcl_basic_cluster_server_config_t basic_config{};
    basic_config.zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE;
    basic_config.power_source = EZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE;
    const auto basic = ezb_zcl_basic_create_cluster_desc(&basic_config, EZB_ZCL_CLUSTER_SERVER);
    if (!add(basic)) return false;
    // ZCL strings are length-prefixed, not C strings on the wire.
    static constexpr char manufacturer[] = "\x08" "B-T HOME";
    static constexpr char model[] = "\x06" "TS0001";
    static_assert(sizeof(model) - 2 == 6, "ZCL model string length must match its prefix");
    if (ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer) != EZB_ERR_NONE ||
        ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model) != EZB_ERR_NONE) return false;
    ezb_zcl_on_off_cluster_server_config_t onoff{};
    onoff.on_off = relay_.snapshot().on;
    if (!add(ezb_zcl_identify_create_cluster_desc(nullptr, EZB_ZCL_CLUSTER_SERVER)) ||
        !add(ezb_zcl_groups_create_cluster_desc(nullptr, EZB_ZCL_CLUSTER_SERVER)) ||
        !add(ezb_zcl_scenes_create_cluster_desc(nullptr, EZB_ZCL_CLUSTER_SERVER)) ||
        !add(ezb_zcl_on_off_create_cluster_desc(&onoff, EZB_ZCL_CLUSTER_SERVER))) return false;
    if (ezb_af_device_desc_register(descriptor_) != EZB_ERR_NONE) return false;
    // Registration retains this descriptor; release it after the stack has stopped.
    return true;
}

Status ZigbeeModule::start() noexcept {
    if (!initialized_ || started_ || !relay_.snapshot().ready) return Status::invalid_state;
    join_ = {}; network_ready_ = resetting_ = cancel_sent_ = false;
    retry_init_ms_ = next_tick_ms_ = 0;
    worker_failed_ = false;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    bool ok = setup_endpoint();
    if (ok) ok = ezb_bdb_set_primary_channel_set(0x07fff800U) == EZB_ERR_NONE &&
                 ezb_bdb_set_secondary_channel_set(0) == EZB_ERR_NONE &&
                 ezb_app_signal_add_handler(signal) == EZB_ERR_NONE;
    if (ok) {
        callbacks_ = true;
        ezb_nwk_set_rx_on_when_idle(true);
        ezb_zcl_core_action_handler_register(action);
        ok = esp_zigbee_start(false) == ESP_OK;
    }
    started_ = ok;
    esp_zigbee_lock_release();
    if (!ok) return Status::hardware_error;
    accepting_ = true;
    if (xTaskCreate(worker, "zigbee", 6144, this, 5, &task_) != pdPASS) {
        accepting_ = false; return Status::no_memory;
    }
    ESP_LOGI(tag, "B-T HOME / TS0001: HA On/Off output, endpoint 1");
    return Status::ok;
}

void ZigbeeModule::worker(void* context) {
    auto& self = *static_cast<ZigbeeModule*>(context);
    const auto result = esp_zigbee_launch_mainloop();
    if (self.accepting_.load() || result != ESP_OK) self.worker_failed_ = true;
    xSemaphoreGive(self.joined_);
    vTaskDelete(nullptr);
}

bool ZigbeeModule::signal(void* const* app_signal) {
    if (!owner_) return false;
    auto& self = *owner_;
    const auto type = ezb_app_signal_get_type(app_signal);
    const auto now = now_ms();
    ESP_LOGI(tag, "Signal: %s", ezb_app_signal_to_string(type));
    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        if (ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION) != EZB_ERR_NONE)
            self.retry_init_ms_ = now + 5000;
        return true;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        const auto* params = static_cast<const ezb_bdb_signal_simple_params_t*>(ezb_app_signal_get_params(app_signal));
        self.network_ready_ = params && params->status == EZB_BDB_STATUS_SUCCESS;
        const unsigned status = params ? params->status : 255U;
        ESP_LOGI(tag, "Startup: status=%u (%s), factory_new=%u, joined=%u, channel=%u",
                 status, commissioning_status(status), static_cast<unsigned>(ezb_bdb_is_factory_new()),
                 static_cast<unsigned>(ezb_bdb_dev_joined()), static_cast<unsigned>(ezb_get_current_channel()));
        if (self.network_ready_) {
            self.retry_init_ms_ = 0;
            if (ezb_bdb_is_factory_new() && !self.resetting_) {
                self.join_.open(now);
                ESP_LOGI(tag, "Pairing window opened: 120s (factory new)");
            }
        } else self.retry_init_ms_ = now + 5000;
        return true;
    }
    case EZB_BDB_SIGNAL_STEERING: {
        const auto* params = static_cast<const ezb_bdb_signal_simple_params_t*>(ezb_app_signal_get_params(app_signal));
        const bool success = params && params->status == EZB_BDB_STATUS_SUCCESS;
        self.join_.finished(now, success);
        self.cancel_sent_ = false;
        const unsigned status = params ? params->status : 255U;
        ESP_LOGI(tag, "Join %s: status=%u (%s), joined=%u, channel=%u",
                 success ? "successful" : "unsuccessful", status, commissioning_status(status),
                 static_cast<unsigned>(ezb_bdb_dev_joined()), static_cast<unsigned>(ezb_get_current_channel()));
        return true;
    }
    case EZB_ZDO_SIGNAL_LEAVE: {
        const auto* params = static_cast<const ezb_zdo_signal_leave_params_t*>(ezb_app_signal_get_params(app_signal));
        self.join_ = {};
        self.resetting_ = self.cancel_sent_ = false;
        self.network_ready_ = params && params->leave_type == EZB_ZDO_LEAVE_TYPE_RESET;
        self.retry_init_ms_ = self.network_ready_ ? 0 : now + 5000;
        return true;
    }
    default: return false;
    }
}

void ZigbeeModule::action(std::uint32_t id, void* data) {
    if (!owner_ || id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || !data) return;
    auto& self = *owner_;
    auto& message = *static_cast<ezb_zcl_set_attr_value_message_t*>(data);
    const auto& attr = message.in.attribute;
    if (message.info.dst_ep != endpoint || message.info.cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF ||
        message.info.cluster_role != EZB_ZCL_CLUSTER_SERVER || attr.id != EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) return;
    if (self.local_update_) { message.out.result = EZB_ZCL_STATUS_SUCCESS; return; }
    if (message.info.status != EZB_ZCL_STATUS_SUCCESS || attr.data.type != EZB_ZCL_ATTR_TYPE_BOOL ||
        attr.data.size != 1 || !attr.data.value || *static_cast<const std::uint8_t*>(attr.data.value) > 1) {
        message.out.result = EZB_ZCL_STATUS_INVALID_VALUE; return;
    }
    const bool on = *static_cast<const std::uint8_t*>(attr.data.value) != 0;
    const bool accepted = self.accepting_.load() && self.relay_.post(on);
    message.out.result = accepted ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_FAIL;
    ESP_LOGI(tag, "Relay %s: %s", on ? "ON" : "OFF", accepted ? "queued" : "rejected");
}

void ZigbeeModule::publish(ZigbeeState state, std::uint64_t now) noexcept {
    const ZigbeeSnapshot value{state, state == ZigbeeState::joined ? ezb_get_current_channel() : 0U,
                              join_.remaining(now)};
    xQueueOverwrite(states_, &value);
}

Status ZigbeeModule::tick(std::uint64_t now) noexcept {
    if (!started_) return Status::invalid_state;
    if (worker_failed_) return Status::hardware_error;
    if (now < next_tick_ms_ || !esp_zigbee_lock_acquire(0)) return Status::ok;
    next_tick_ms_ = now + 100;
    ZigbeeCommand command;
    // Only one web command per tick. All SDK state is protected by its lock.
    if (xQueueReceive(commands_, &command, 0) == pdTRUE) {
        if (command == ZigbeeCommand::reset && !resetting_) {
            join_.close(); resetting_ = true;
            ezb_bdb_reset_via_local_action();
        } else if (command == ZigbeeCommand::join && network_ready_ && !resetting_ && !ezb_bdb_dev_joined()) {
            join_.open(now);
            ESP_LOGI(tag, "Pairing window opened: 120s (button/request)");
        } else if (command == ZigbeeCommand::join) {
            ESP_LOGW(tag, "Pairing not started: ready=%u, resetting=%u, joined=%u",
                     static_cast<unsigned>(network_ready_), static_cast<unsigned>(resetting_),
                     static_cast<unsigned>(ezb_bdb_dev_joined()));
        }
    }
    if (retry_init_ms_ && now >= retry_init_ms_ && !resetting_) {
        retry_init_ms_ = 0;
        if (ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION) != EZB_ERR_NONE)
            retry_init_ms_ = now + 5000;
    }
    const bool connected = ezb_bdb_dev_joined();
    if (connected) join_.close();
    if (join_.needs_cancel(now) && !cancel_sent_ && !connected) {
        cancel_sent_ = ezb_bdb_cancel_steering() == EZB_ERR_NONE;
    }
    if (network_ready_ && !connected && !resetting_ && join_.due(now)) {
        join_.started();
        const auto result = ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        ESP_LOGI(tag, "Steering requested: channels=11-26, result=%d, window_left=%us",
                 static_cast<int>(result), static_cast<unsigned>(join_.remaining(now)));
        if (result != EZB_ERR_NONE)
            join_.finished(now, false);
    }
    // Reconcile with the actual relay worker, including web changes and rejected writes.
    if (relay_.idle()) {
        const auto state = relay_.snapshot();
        const auto attr = ezb_zcl_get_attr_desc(endpoint, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                                               EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE);
        bool previous = false;
        if (state.output_known && attr && ezb_zcl_attr_desc_get_value(attr, &previous) == EZB_ERR_NONE && previous != state.on) {
            bool on = state.on;
            local_update_ = true;
            (void)ezb_zcl_set_attr_value(endpoint, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_CLUSTER_SERVER,
                                       EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, EZB_ZCL_STD_MANUF_CODE, &on, false);
            local_update_ = false;
        }
    }
    publish(resetting_ ? ZigbeeState::resetting : connected ? ZigbeeState::joined :
            !network_ready_ ? ZigbeeState::rejoining : join_.active(now) ? ZigbeeState::joining : ZigbeeState::idle, now);
    esp_zigbee_lock_release();
    return Status::ok;
}

ZigbeeSnapshot ZigbeeModule::snapshot() const noexcept {
    ZigbeeSnapshot result{};
    if (states_) (void)xQueuePeek(states_, &result, 0);
    return result;
}
bool ZigbeeModule::request(ZigbeeCommand command) noexcept {
    return accepting_.load() && commands_ && xQueueSend(commands_, &command, 0) == pdTRUE;
}

void ZigbeeModule::stop() noexcept {
    accepting_ = false;
    if (initialized_ && started_) {
        esp_zigbee_lock_acquire(portMAX_DELAY);
        (void)esp_zigbee_stop();
        esp_zigbee_lock_release();
        if (task_) { xSemaphoreTake(joined_, portMAX_DELAY); task_ = nullptr; }
        started_ = false;
    }
}
void ZigbeeModule::deinit() noexcept {
    stop();
    if (initialized_) {
        esp_zigbee_lock_acquire(portMAX_DELAY);
        if (callbacks_) {
            ezb_app_signal_remove_handler(signal);
            ezb_zcl_core_action_handler_register(nullptr);
            callbacks_ = false;
        }
        if (descriptor_) { ezb_af_free_device_desc(descriptor_); descriptor_ = nullptr; }
        esp_zigbee_lock_release();
        (void)esp_zigbee_deinit();
        initialized_ = false;
    }
    if (owner_ == this) owner_ = nullptr;
    // SDK dataset handles must be closed before releasing the partition.
    storage_.deinit();
    if (commands_) { vQueueDelete(commands_); commands_ = nullptr; }
    if (states_) { vQueueDelete(states_); states_ = nullptr; }
    if (joined_) { vSemaphoreDelete(joined_); joined_ = nullptr; }
}
}  // namespace bth
