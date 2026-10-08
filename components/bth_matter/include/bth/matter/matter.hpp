#pragma once
#include "bth/core/module.hpp"
#include "bth/platform/relay_service.hpp"
#include "bth/matter/relay_gate.hpp"
#include "bth/matter/pairing_retry.hpp"
#include "bth/matter/measurement_access.hpp"
#include "bth/matter/voltage_value.hpp"
#include "bth/matter/product_info.hpp"
#include <platform/ESP32/StaticESP32DeviceInfoProvider.h>
#include <esp_matter.h>
#include <atomic>
#include <array>

namespace bth {
class MatterModule final : public Module {
public:
    explicit MatterModule(RelayService& relay) noexcept
        : relay_(relay) {}
    const char* id() const noexcept override { return "matter"; }
    Dependencies dependencies() const noexcept override { return {required_, 2}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    void open_pairing(bool reset_matter = false) noexcept { pairing_retry_.request(reset_matter); }
    void report_mains_voltage(double volts, bool valid) noexcept;
    void report_backup_voltage(double volts, bool valid) noexcept;
    // Owner task calls this outside Runtime::tick: explicit Matter reset writes NVS.
    void service_pairing() noexcept;
    bool pairing() const noexcept { return pairing_; }
    // Owner task only, outside Runtime::tick. Zero means no usable STA address.
    std::array<std::uint8_t, 4> ipv4_address() const noexcept;
private:
    inline static constexpr const char* required_[] = {"nvs", "relay"};
    static esp_err_t attribute_changed(esp_matter::attribute::callback_type_t type,
        std::uint16_t endpoint, std::uint32_t cluster, std::uint32_t attribute,
        esp_matter_attr_val_t* value, void* context);
    static void event(const chip::DeviceLayer::ChipDeviceEvent* event, intptr_t context);
    RelayService& relay_;
    esp_matter::node_t* node_ = nullptr;
    std::uint16_t endpoint_ = 0;
    std::uint16_t backup_endpoint_ = 0;
    std::uint16_t temperature_endpoint_ = 0;
    std::uint16_t mains_sensor_endpoint_ = 0;
    std::uint16_t backup_temperature_endpoint_ = 0;
    std::uint16_t average_temperature_endpoint_ = 0;
    std::int64_t mains_millivolts_ = 0, backup_millivolts_ = 0;
    bool mains_valid_ = false, backup_valid_ = false;
    bool mains_dirty_ = false, backup_dirty_ = false;
    std::int16_t compatibility_temperature_centi_degrees_ = 0;
    bool compatibility_temperature_valid_ = false, compatibility_temperature_dirty_ = false;
    std::int16_t backup_temperature_centi_degrees_ = 0;
    bool backup_temperature_valid_ = false, backup_temperature_dirty_ = false;
    RollingMainsVoltageAverage mains_average_;
    std::int16_t average_temperature_centi_degrees_ = 0;
    bool average_temperature_valid_ = false, average_temperature_dirty_ = false;
    MatterRelayGate relay_gate_;
    MeasurementAccess measurement_access_;
    ProductInfo product_info_;
    chip::DeviceLayer::DeviceInstanceInfoProvider* previous_product_info_ = nullptr;
    chip::DeviceLayer::StaticESP32DeviceInfoProvider label_provider_;
    chip::DeviceLayer::StaticESP32DeviceInfoProvider::FixedLabelEntry labels_[4]{};
    chip::DeviceLayer::DeviceInfoProvider* previous_label_provider_ = nullptr;
    bool presentation_installed_ = false;
    bool measurement_access_registered_ = false;
    std::atomic<bool> pairing_{false};
    bool storage_ = false, start_attempted_ = false, started_ = false, consumed_ = false;
    bool local_update_ = false;
    PairingRetry pairing_retry_;
    std::uint64_t next_tick_ = 0;
};
}  // namespace bth
