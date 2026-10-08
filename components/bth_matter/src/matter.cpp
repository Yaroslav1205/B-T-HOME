#include "bth/matter/matter.hpp"
#include "bth/matter/voltage_value.hpp"
#include <app/AttributeAccessInterfaceRegistry.h>
#include <cmath>
#include <cstring>
#include <app/server/Server.h>
#include <app/server/CommissioningWindowManager.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

namespace bth {
namespace {
constexpr const char* tag = "matter";
static_assert(CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT >= 7,
              "Matter requires root, plug, two electrical and three temperature sensor endpoints");
using namespace chip::app::Clusters;
bool add_voltage_attributes(esp_matter::endpoint_t* endpoint, bool ac, MatterVoltageValue initial) {
    auto* cluster = esp_matter::cluster::get(endpoint, ElectricalPowerMeasurement::Id);
    if (!cluster) return false;
    // Application-owned storage: voltage is null until a valid ADC report.
    const auto add = [cluster, initial](std::uint32_t id) {
        return esp_matter::attribute::create(cluster, id, esp_matter::ATTRIBUTE_FLAG_NULLABLE,
            esp_matter_nullable_int64(initial.valid ? nullable<std::int64_t>(initial.millivolts)
                                                   : nullable<std::int64_t>())) != nullptr;
    };
    return add(ElectricalPowerMeasurement::Attributes::Voltage::Id) &&
        (!ac || add(ElectricalPowerMeasurement::Attributes::RMSVoltage::Id));
}
void log_wifi_configuration() {
    wifi_config_t config{};
    const auto result = esp_wifi_get_config(WIFI_IF_STA, &config);
    if (result == ESP_OK) {
        ESP_LOGI(tag, "Wi-Fi configured SSID: '%.*s' (password hidden)",
                 static_cast<int>(sizeof(config.sta.ssid)), reinterpret_cast<const char*>(config.sta.ssid));
    } else {
        ESP_LOGW(tag, "Wi-Fi configuration unavailable: %s", esp_err_to_name(result));
    }
    auto& driver = chip::DeviceLayer::NetworkCommissioning::ESPWiFiDriver::GetInstance();
    if (auto* networks = driver.GetNetworks()) {
        ESP_LOGI(tag, "Matter Wi-Fi network slots: %u/%u", static_cast<unsigned>(networks->Count()),
                 static_cast<unsigned>(driver.GetMaxNetworks()));
        networks->Release();
    }
}
}
Status MatterModule::init() noexcept {
    if (storage_ || node_ || consumed_) return Status::invalid_state;
    const auto result = nvs_flash_init_partition("matter_nvs");
    if (result != ESP_OK) {
        ESP_LOGE(tag, "Matter NVS: %s; storage will not be erased", esp_err_to_name(result));
        return Status::hardware_error;
    }
    storage_ = true;
    esp_matter::node::config_t node_config{};
    std::memcpy(node_config.root_node.basic_information.node_label, "B-T HOME", sizeof("B-T HOME"));
    node_ = esp_matter::node::create(&node_config, attribute_changed, nullptr);
    if (!node_) return Status::no_memory;
    esp_matter::endpoint::on_off_plugin_unit::config_t relay_config{};
    relay_config.on_off.on_off = relay_.snapshot().on;
    relay_config.on_off_lighting.start_up_on_off = nullptr;
    const auto endpoint = esp_matter::endpoint::on_off_plugin_unit::create(
        node_, &relay_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!endpoint) return Status::no_memory;
    endpoint_ = esp_matter::endpoint::get_id(endpoint);

    esp_matter::endpoint::electrical_sensor::config_t mains_config{};
    mains_config.power_topology.feature_flags =
        esp_matter::cluster::power_topology::feature::node_topology::get_id();
    mains_config.electrical_power_measurement.feature_flags =
        esp_matter::cluster::electrical_power_measurement::feature::alternating_current::get_id();
    // Enrich the existing plug endpoint so controllers need not expose a
    // standalone utility device to discover its voltage measurements.
    if (esp_matter::endpoint::electrical_sensor::add(endpoint, &mains_config) != ESP_OK ||
        !add_voltage_attributes(endpoint, true, {mains_millivolts_, mains_valid_})) return Status::no_memory;
    esp_matter::endpoint::electrical_sensor::config_t backup_config{};
    backup_config.power_topology.feature_flags =
        esp_matter::cluster::power_topology::feature::node_topology::get_id();
    backup_config.electrical_power_measurement.feature_flags =
        esp_matter::cluster::electrical_power_measurement::feature::direct_current::get_id();
    const auto backup_endpoint = esp_matter::endpoint::electrical_sensor::create(
        node_, &backup_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!backup_endpoint) return Status::no_memory;
    backup_endpoint_ = esp_matter::endpoint::get_id(backup_endpoint);
    if (!add_voltage_attributes(backup_endpoint, false, {backup_millivolts_, backup_valid_})) return Status::no_memory;

    // Keep the temperature endpoint type for Smart Life compatibility; its value
    // follows measured volts using the same numeric scale (volts * 100).
    esp_matter::endpoint::temperature_sensor::config_t temperature_config{};
    temperature_config.temperature_measurement.measured_value = compatibility_temperature_valid_
        ? nullable<std::int16_t>(compatibility_temperature_centi_degrees_)
        : nullable<std::int16_t>();
    temperature_config.temperature_measurement.min_measured_value = nullable<std::int16_t>(std::int16_t{0});
    temperature_config.temperature_measurement.max_measured_value = nullable<std::int16_t>(std::int16_t{30000});
    const auto temperature_endpoint = esp_matter::endpoint::temperature_sensor::create(
        node_, &temperature_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!temperature_endpoint) return Status::no_memory;
    temperature_endpoint_ = esp_matter::endpoint::get_id(temperature_endpoint);
    esp_matter::cluster::fixed_label::config_t label_config{};
    if (!esp_matter::cluster::fixed_label::create(temperature_endpoint, &label_config,
                                                 esp_matter::CLUSTER_FLAG_SERVER)) return Status::no_memory;
    labels_[0] = {temperature_endpoint_, chip::CharSpan::fromCharString("name"),
                  chip::CharSpan::fromCharString("Напруга")};

    // Append the AC sensor to preserve all existing endpoint IDs and panels.
    const auto mains_sensor = esp_matter::endpoint::electrical_sensor::create(
        node_, &mains_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!mains_sensor) return Status::no_memory;
    mains_sensor_endpoint_ = esp_matter::endpoint::get_id(mains_sensor);
    if (!add_voltage_attributes(mains_sensor, true, {mains_millivolts_, mains_valid_}) ||
        !esp_matter::cluster::fixed_label::create(mains_sensor, &label_config,
                                                 esp_matter::CLUSTER_FLAG_SERVER)) return Status::no_memory;
    labels_[1] = {mains_sensor_endpoint_, chip::CharSpan::fromCharString("name"),
                  chip::CharSpan::fromCharString("Напруга")};
    measurement_access_.configure(endpoint_, backup_endpoint_, mains_sensor_endpoint_);

    // A second compatibility panel displays measured DC voltage as temperature.
    // Append it after existing endpoints so their IDs remain unchanged.
    esp_matter::endpoint::temperature_sensor::config_t backup_temperature_config{};
    backup_temperature_config.temperature_measurement.measured_value = backup_temperature_valid_
        ? nullable<std::int16_t>(backup_temperature_centi_degrees_)
        : nullable<std::int16_t>();
    backup_temperature_config.temperature_measurement.min_measured_value = nullable<std::int16_t>(std::int16_t{0});
    backup_temperature_config.temperature_measurement.max_measured_value = nullable<std::int16_t>(std::int16_t{3005});
    const auto backup_temperature = esp_matter::endpoint::temperature_sensor::create(
        node_, &backup_temperature_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!backup_temperature) return Status::no_memory;
    backup_temperature_endpoint_ = esp_matter::endpoint::get_id(backup_temperature);
    if (!esp_matter::cluster::fixed_label::create(backup_temperature, &label_config,
                                                 esp_matter::CLUSTER_FLAG_SERVER)) return Status::no_memory;
    labels_[2] = {backup_temperature_endpoint_, chip::CharSpan::fromCharString("name"),
                  chip::CharSpan::fromCharString("Резерв")};

    // Append the rolling mains average without changing existing endpoint IDs.
    esp_matter::endpoint::temperature_sensor::config_t average_temperature_config{};
    average_temperature_config.temperature_measurement.measured_value = average_temperature_valid_
        ? nullable<std::int16_t>(average_temperature_centi_degrees_)
        : nullable<std::int16_t>();
    average_temperature_config.temperature_measurement.min_measured_value = nullable<std::int16_t>(std::int16_t{0});
    average_temperature_config.temperature_measurement.max_measured_value = nullable<std::int16_t>(std::int16_t{30000});
    const auto average_temperature = esp_matter::endpoint::temperature_sensor::create(
        node_, &average_temperature_config, esp_matter::ENDPOINT_FLAG_NONE, this);
    if (!average_temperature) return Status::no_memory;
    average_temperature_endpoint_ = esp_matter::endpoint::get_id(average_temperature);
    if (!esp_matter::cluster::fixed_label::create(average_temperature, &label_config,
                                                 esp_matter::CLUSTER_FLAG_SERVER)) return Status::no_memory;
    labels_[3] = {average_temperature_endpoint_, chip::CharSpan::fromCharString("name"),
                  chip::CharSpan::fromCharString("Середня напруга")};
    if (label_provider_.SetFixedLabels(chip::Span<chip::DeviceLayer::StaticESP32DeviceInfoProvider::FixedLabelEntry>(labels_, 4)) != CHIP_NO_ERROR)
        return Status::internal_error;
    ESP_LOGI(tag, "Compatibility backup display follows measured DC voltage (temperature scale)");
    ESP_LOGI(tag, "Compatibility voltage display follows measured mains voltage (temperature scale)");
    ESP_LOGI(tag, "Average mains voltage display endpoint=%u; rolling window: 60 valid reports",
             average_temperature_endpoint_);
    return Status::ok;
}

void MatterModule::report_mains_voltage(double volts, bool valid) noexcept {
    const auto value = matter_voltage_value(volts, valid);
    const bool usable = value.valid;
    const auto millivolts = value.millivolts;
    if (usable != mains_valid_ || (usable && millivolts != mains_millivolts_)) {
        mains_valid_ = usable;
        mains_millivolts_ = millivolts;
        mains_dirty_ = true;
    }
    const auto temperature = compatibility_temperature_value(volts, valid);
    if (temperature.valid != compatibility_temperature_valid_ ||
        (temperature.valid && temperature.centi_degrees != compatibility_temperature_centi_degrees_)) {
        compatibility_temperature_valid_ = temperature.valid;
        compatibility_temperature_centi_degrees_ = temperature.centi_degrees;
        compatibility_temperature_dirty_ = true;
    }
    const auto average = mains_average_.add(volts, valid);
    if (average.valid != average_temperature_valid_ ||
        (average.valid && average.centi_degrees != average_temperature_centi_degrees_)) {
        average_temperature_valid_ = average.valid;
        average_temperature_centi_degrees_ = average.centi_degrees;
        average_temperature_dirty_ = true;
    }
}

void MatterModule::report_backup_voltage(double volts, bool valid) noexcept {
    const auto value = matter_voltage_value(volts, valid);
    const bool usable = value.valid;
    const auto millivolts = value.millivolts;
    if (usable != backup_valid_ || (usable && millivolts != backup_millivolts_)) {
        backup_valid_ = usable;
        backup_millivolts_ = millivolts;
        backup_dirty_ = true;
    }
    const auto temperature = compatibility_temperature_value(volts, valid && volts < 30.05);
    if (temperature.valid != backup_temperature_valid_ ||
        (temperature.valid && temperature.centi_degrees != backup_temperature_centi_degrees_)) {
        backup_temperature_valid_ = temperature.valid;
        backup_temperature_centi_degrees_ = temperature.centi_degrees;
        backup_temperature_dirty_ = true;
    }
}

Status MatterModule::start() noexcept {
    if (!node_ || start_attempted_ || !relay_.snapshot().ready) return Status::invalid_state;
    start_attempted_ = consumed_ = true;
    const auto result = esp_matter::start(event, reinterpret_cast<intptr_t>(this));
    // The SDK can report a final NVS error after starting its server and tasks.
    started_ = esp_matter::is_started();
    if (result != ESP_OK) {
        ESP_LOGE(tag, "Start: %s", esp_err_to_name(result));
        return Status::hardware_error;
    }
    next_tick_ = 0;
    chip::DeviceLayer::PlatformMgr().LockChipStack();
    previous_product_info_ = chip::DeviceLayer::GetDeviceInstanceInfoProvider();
    if (!previous_product_info_) {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return Status::invalid_state;
    }
    product_info_.attach(*previous_product_info_);
    chip::DeviceLayer::SetDeviceInstanceInfoProvider(&product_info_);
    previous_label_provider_ = chip::DeviceLayer::GetDeviceInfoProvider();
    chip::DeviceLayer::SetDeviceInfoProvider(&label_provider_);
    presentation_installed_ = true;
    char product_name[33]{};
    if (chip::DeviceLayer::GetDeviceInstanceInfoProvider()->GetProductName(product_name, sizeof(product_name)) != CHIP_NO_ERROR) {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        return Status::hardware_error;
    }
    ESP_LOGI(tag, "Matter ProductName: %s", product_name);
    measurement_access_registered_ = chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&measurement_access_);
    if (!measurement_access_registered_) {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        ESP_LOGE(tag, "Electrical measurement metadata handler registration failed");
        return Status::invalid_state;
    }
    ESP_LOGI(tag, "Electrical measurement metadata ready: AC/DC, Accuracy, unknown ActivePower");
    // SDK restoration is complete. Align its OnOff with the physical output
    // while the gate still ignores writes, before accepting controller commands.
    auto on_off = esp_matter_bool(relay_.snapshot().on);
    const auto sync_result = esp_matter::attribute::update(endpoint_, OnOff::Id,
        OnOff::Attributes::OnOff::Id, &on_off);
    if (sync_result != ESP_OK) {
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        ESP_LOGE(tag, "Initial relay state sync failed: %s", esp_err_to_name(sync_result));
        return Status::hardware_error;
    }
    relay_gate_.start();
    log_wifi_configuration();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
    pairing_ = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    ESP_LOGI(tag, "B-T HOME Matter | metered_plug=%u backup=%u voltage_display=%u mains_sensor=%u; pair using the onboarding code above",
             endpoint_, backup_endpoint_, temperature_endpoint_, mains_sensor_endpoint_);
    ESP_LOGI(tag, "ProductName/ProductLabel: B-T HOME; display label: Напруга");
    ESP_LOGI(tag, "Backup temperature display endpoint=%u; label: Резерв; measured DC voltage",
             backup_temperature_endpoint_);
    return Status::ok;
}
esp_err_t MatterModule::attribute_changed(esp_matter::attribute::callback_type_t type,
    std::uint16_t endpoint, std::uint32_t cluster, std::uint32_t attribute,
    esp_matter_attr_val_t* value, void* context) {
    if (!context || type != esp_matter::attribute::PRE_UPDATE || cluster != OnOff::Id ||
        attribute != OnOff::Attributes::OnOff::Id) return ESP_OK;
    auto& self = *static_cast<MatterModule*>(context);
    if (endpoint != self.endpoint_) return ESP_OK;
    const auto decision = self.relay_gate_.classify(self.local_update_);
    if (decision == MatterWrite::ignore) return ESP_OK;
    if (decision == MatterWrite::reject) return ESP_ERR_INVALID_STATE;
    if (!value || value->type != ESP_MATTER_VAL_TYPE_BOOLEAN) return ESP_ERR_INVALID_ARG;
    return self.relay_.post(value->val.b) ? ESP_OK : ESP_FAIL;
}
void MatterModule::event(const chip::DeviceLayer::ChipDeviceEvent* ev, intptr_t context) {
    auto& self = *reinterpret_cast<MatterModule*>(context);
    using namespace chip::DeviceLayer;
    switch (ev->Type) {
    case DeviceEventType::kESPSystemEvent: {
        const auto& system = ev->Platform.ESPSystemEvent;
        if (system.Base == WIFI_EVENT && system.Id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto& disconnected = system.Data.WiFiStaDisconnected;
            ESP_LOGW(tag, "Wi-Fi disconnected: reason=%u SSID='%.*s'",
                     static_cast<unsigned>(disconnected.reason),
                     static_cast<int>(disconnected.ssid_len),
                     reinterpret_cast<const char*>(disconnected.ssid));
        }
        break;
    }
    case DeviceEventType::kCommissioningWindowOpened: self.pairing_ = true; ESP_LOGI(tag, "Pairing window open"); break;
    case DeviceEventType::kCommissioningWindowClosed: self.pairing_ = false; ESP_LOGI(tag, "Pairing window closed"); break;
    case DeviceEventType::kCommissioningComplete: ESP_LOGI(tag, "Commissioning complete"); break;
    case DeviceEventType::kInterfaceIpAddressChanged: {
        auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip{};
        if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr)
            ESP_LOGI(tag, "Web: http://" IPSTR, IP2STR(&ip.ip));
        break;
    }
    default: break;
    }
}
void MatterModule::service_pairing() noexcept {
    if (!started_ || !pairing_retry_.pending()) return;
    auto& platform = chip::DeviceLayer::PlatformMgr();
    if (!platform.TryLockChipStack()) return;
    {
        auto& manager = chip::Server::GetInstance().GetCommissioningWindowManager();
        auto& fail_safe = chip::Server::GetInstance().GetFailSafeContext();
        const auto action = pairing_retry_.advance(fail_safe.IsFailSafeFullyDisarmed());
        if (action == PairingAction::rollback) {
            // A physical BOOT retry cancels only the unfinished commissioning
            // transaction. SDK rollback completes asynchronously before reopening.
            manager.CloseCommissioningWindow();
            if (fail_safe.IsFailSafeArmed()) fail_safe.ForceFailSafeTimerExpiry();
            ESP_LOGI(tag, "BOOT: finishing previous commissioning attempt; pairing remains queued");
        } else if (action == PairingAction::factory_reset) {
            relay_gate_.stop();
            ESP_LOGI(tag, "BOOT: resetting Matter bindings and Wi-Fi; rebooting for new pairing");
            // The SDK schedules its reset even if clearing its attribute store fails.
            // Never retry here: the one-shot request remains latched until reboot.
            const auto result = esp_matter::factory_reset();
            if (result != ESP_OK)
                ESP_LOGE(tag, "Matter attribute reset: %s; SDK factory reset remains scheduled", esp_err_to_name(result));
        } else if (action == PairingAction::open) {
            auto result = CHIP_NO_ERROR;
            if (!manager.IsCommissioningWindowOpen())
                result = manager.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300));
            if (result != CHIP_NO_ERROR) {
                ESP_LOGW(tag, "Pairing request failed: %" CHIP_ERROR_FORMAT, result.Format());
            } else {
                ESP_LOGI(tag, "BOOT: pairing window ready");
                log_wifi_configuration();
                PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
            }
        }
        pairing_ = manager.IsCommissioningWindowOpen();
    }
    platform.UnlockChipStack();
}
Status MatterModule::tick(std::uint64_t now) noexcept {
    if (!started_) return Status::invalid_state;
    if (now < next_tick_) return Status::ok;
    next_tick_ = now + 100;
    // ESP-Matter 1.4.2's chip_stack_lock(0) skips the actual lock attempt.
    auto& platform = chip::DeviceLayer::PlatformMgr();
    if (!platform.TryLockChipStack()) return Status::ok;
    pairing_ = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
    if (relay_.idle()) {
        const auto state = relay_.snapshot();
        esp_matter_attr_val_t previous = esp_matter_invalid(nullptr);
        auto* attr = esp_matter::attribute::get(endpoint_, OnOff::Id, OnOff::Attributes::OnOff::Id);
        if (state.output_known && attr && esp_matter::attribute::get_val(attr, &previous) == ESP_OK &&
            previous.val.b != state.on) {
            auto value = esp_matter_bool(state.on);
            local_update_ = true;
            (void)esp_matter::attribute::update(endpoint_, OnOff::Id, OnOff::Attributes::OnOff::Id, &value);
            local_update_ = false;
        }
    }
    const auto publish_voltage = [](std::uint16_t endpoint, std::uint32_t attribute,
                                    bool valid, std::int64_t millivolts) {
        auto measured = valid
            ? esp_matter_nullable_int64(nullable<std::int64_t>(millivolts))
            : esp_matter_nullable_int64(nullable<std::int64_t>());
        return esp_matter::attribute::update(endpoint, ElectricalPowerMeasurement::Id,
                                             attribute, &measured) == ESP_OK;
    };
    if (mains_dirty_) {
        bool published = true;
        for (const auto attribute : {ElectricalPowerMeasurement::Attributes::Voltage::Id,
                                     ElectricalPowerMeasurement::Attributes::RMSVoltage::Id}) {
            // Attempt every copy; retry the report if any update fails.
            for (const auto endpoint : {endpoint_, mains_sensor_endpoint_}) {
                published = publish_voltage(endpoint, attribute, mains_valid_, mains_millivolts_) && published;
            }
        }
        mains_dirty_ = !published;
    }
    if (compatibility_temperature_dirty_) {
        auto measured = compatibility_temperature_valid_
            ? esp_matter_nullable_int16(nullable<std::int16_t>(compatibility_temperature_centi_degrees_))
            : esp_matter_nullable_int16(nullable<std::int16_t>());
        if (esp_matter::attribute::update(temperature_endpoint_, TemperatureMeasurement::Id,
                TemperatureMeasurement::Attributes::MeasuredValue::Id, &measured) == ESP_OK) {
            compatibility_temperature_dirty_ = false;
        }
    }
    if (backup_temperature_dirty_) {
        auto measured = backup_temperature_valid_
            ? esp_matter_nullable_int16(nullable<std::int16_t>(backup_temperature_centi_degrees_))
            : esp_matter_nullable_int16(nullable<std::int16_t>());
        if (esp_matter::attribute::update(backup_temperature_endpoint_, TemperatureMeasurement::Id,
                TemperatureMeasurement::Attributes::MeasuredValue::Id, &measured) == ESP_OK) {
            backup_temperature_dirty_ = false;
        }
    }
    if (average_temperature_dirty_) {
        auto measured = average_temperature_valid_
            ? esp_matter_nullable_int16(nullable<std::int16_t>(average_temperature_centi_degrees_))
            : esp_matter_nullable_int16(nullable<std::int16_t>());
        if (esp_matter::attribute::update(average_temperature_endpoint_, TemperatureMeasurement::Id,
                TemperatureMeasurement::Attributes::MeasuredValue::Id, &measured) == ESP_OK) {
            average_temperature_dirty_ = false;
        }
    }
    if (backup_dirty_ && publish_voltage(backup_endpoint_,
            ElectricalPowerMeasurement::Attributes::Voltage::Id,
            backup_valid_, backup_millivolts_)) {
        backup_dirty_ = false;
    }
    platform.UnlockChipStack();
    return Status::ok;
}
std::array<std::uint8_t, 4> MatterModule::ipv4_address() const noexcept {
    if (!started_) return {};
    wifi_ap_record_t ap{};
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    // IP info can retain an old lease while disconnected; check link state too.
    if (!netif || esp_wifi_sta_get_ap_info(&ap) != ESP_OK || !esp_netif_is_netif_up(netif) ||
        esp_netif_get_ip_info(netif, &ip) != ESP_OK || !ip.ip.addr) return {};
    return {esp_ip4_addr1(&ip.ip), esp_ip4_addr2(&ip.ip),
            esp_ip4_addr3(&ip.ip), esp_ip4_addr4(&ip.ip)};
}

void MatterModule::stop() noexcept {
    relay_gate_.stop();
    if (!start_attempted_) return;
    if (started_) {
        chip::DeviceLayer::PlatformMgr().LockChipStack();
        if (presentation_installed_) {
            chip::DeviceLayer::SetDeviceInstanceInfoProvider(previous_product_info_);
            chip::DeviceLayer::SetDeviceInfoProvider(previous_label_provider_);
            presentation_installed_ = false;
        }
        if (measurement_access_registered_) {
            chip::app::AttributeAccessInterfaceRegistry::Instance().Unregister(&measurement_access_);
            measurement_access_registered_ = false;
        }
        chip::DeviceLayer::PlatformMgr().RemoveEventHandler(event, reinterpret_cast<intptr_t>(this));
        chip::Server::GetInstance().Shutdown();
        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
    (void)chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    chip::DeviceLayer::PlatformMgr().Shutdown();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    (void)esp_event_loop_delete_default();
    started_ = start_attempted_ = false;
    pairing_ = false;
}
void MatterModule::deinit() noexcept {
    stop();
    if (node_) { (void)esp_matter::node::destroy(); node_ = nullptr; }
    if (storage_) { (void)nvs_flash_deinit_partition("matter_nvs"); storage_ = false; }
}
}  // namespace bth
