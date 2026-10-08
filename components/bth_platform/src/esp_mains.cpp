#include "bth/platform/esp_mains.hpp"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_adc/adc_cali_scheme.h"
#include "soc/soc_caps.h"
#include <algorithm>
#include <cstring>

namespace bth {
namespace {
Status checked(esp_err_t error) {
    if (error == ESP_OK) return Status::ok;
    ESP_LOGE("mains", "ADC: %s", esp_err_to_name(error));
    return Status::hardware_error;
}
}
bool IRAM_ATTR EspMainsInput::overflow(adc_continuous_handle_t,
    const adc_continuous_evt_data_t*, void* context) {
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_cast<EspMainsInput*>(context)->overflow_.store(true, std::memory_order_relaxed);
    return false;
}
Status EspMainsInput::init() noexcept {
    if (handle_ || dc_calibration_) return Status::invalid_state;
#if CONFIG_IDF_TARGET_ESP32C6
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_continuous_io_to_channel(gpio_, &unit, &channel) != ESP_OK || unit != ADC_UNIT_1)
        return Status::invalid_argument;
    mains_channel_ = channel;
    adc_channel_t dc_channel = ADC_CHANNEL_0;
    if (dc_gpio_ >= 0) {
        adc_unit_t dc_unit;
        if (dc_gpio_ == gpio_ || adc_continuous_io_to_channel(dc_gpio_, &dc_unit, &dc_channel) != ESP_OK ||
            dc_unit != ADC_UNIT_1) return Status::invalid_argument;
        dc_channel_ = dc_channel;
        adc_cali_curve_fitting_config_t calibration{};
        calibration.unit_id = ADC_UNIT_1;
        calibration.chan = dc_channel;
        calibration.atten = ADC_ATTEN_DB_12;
        calibration.bitwidth = ADC_BITWIDTH_12;
        const auto error = adc_cali_create_scheme_curve_fitting(&calibration, &dc_calibration_);
        if (error == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW("dc", "ADC calibration unavailable; DC readings will be invalid");
        } else if (error != ESP_OK) return checked(error);
    }
    adc_continuous_handle_cfg_t handle_config{};
    handle_config.max_store_buf_size = 4096;
    handle_config.conv_frame_size = 128;
    auto status = checked(adc_continuous_new_handle(&handle_config, &handle_));
    if (status != Status::ok) return status;
    adc_digi_pattern_config_t pattern[2]{};
    pattern[0].atten = ADC_ATTEN_DB_12;
    pattern[0].channel = channel;
    pattern[0].unit = unit;
    pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    pattern[1] = pattern[0];
    pattern[1].channel = dc_channel;
    adc_continuous_config_t config{};
    config.pattern_num = dc_gpio_ >= 0 ? 2 : 1;
    config.adc_pattern = pattern;
    config.sample_freq_hz = 2000 * config.pattern_num;
    config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    status = checked(adc_continuous_config(handle_, &config));
    if (status != Status::ok) return status;
    adc_continuous_evt_cbs_t callbacks{};
    callbacks.on_pool_ovf = overflow;
    return checked(adc_continuous_register_event_callbacks(handle_, &callbacks, this));
#else
    return Status::invalid_argument;
#endif
}
Status EspMainsInput::start() noexcept {
    if (!handle_ || running_) return Status::invalid_state;
    auto status = checked(adc_continuous_flush_pool(handle_));
    if (status != Status::ok) return status;
    overflow_.store(false);
    dc_batch_ = {};
    status = checked(adc_continuous_start(handle_));
    running_ = status == Status::ok;
    return status;
}
Status EspMainsInput::read(std::uint16_t* samples, std::size_t capacity,
                            std::size_t& count, bool& loss) noexcept {
    count = 0;
    loss = overflow_.exchange(false);
    dc_batch_.invalid = dc_batch_.invalid || loss;
    if (!running_) return Status::invalid_state;
    alignas(adc_digi_output_data_t) std::uint8_t buffer[1024];
    std::uint32_t bytes = 0;
    const auto limit = std::min(capacity, sizeof(buffer) / SOC_ADC_DIGI_RESULT_BYTES);
    const auto error = adc_continuous_read(handle_, buffer,
        static_cast<std::uint32_t>(limit * SOC_ADC_DIGI_RESULT_BYTES), &bytes, 0);
    if (error == ESP_ERR_TIMEOUT) return Status::ok;
    if (error != ESP_OK) return checked(error);
#if CONFIG_IDF_TARGET_ESP32C6
    for (std::size_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= bytes; i += SOC_ADC_DIGI_RESULT_BYTES) {
        adc_digi_output_data_t result{};
        std::memcpy(&result, buffer + i, sizeof(result));
        const auto raw = result.type2.data;
        if (result.type2.channel == mains_channel_) {
            samples[count++] = raw;
        } else if (dc_gpio_ >= 0 && result.type2.channel == dc_channel_) {
            int millivolts = 0;
            if (!dc_calibration_ || raw >= 4095 || dc_batch_.samples >= 4096) {
                dc_batch_.invalid = true;
                continue;
            }
            const auto calibrated = adc_cali_raw_to_voltage(dc_calibration_, raw, &millivolts);
            if (calibrated != ESP_OK) return checked(calibrated);
            if (millivolts < 0) { dc_batch_.invalid = true; continue; }
            dc_batch_.millivolt_sum += millivolts;
            ++dc_batch_.samples;
        } else {
            loss = true;
        }
    }
#else
    (void)samples;
#endif
    loss = overflow_.exchange(false) || loss;
    dc_batch_.invalid = dc_batch_.invalid || loss;
    return Status::ok;
}
Status EspMainsInput::read_dc(DcBatch& batch) noexcept {
    if (!running_ || dc_gpio_ < 0) return Status::invalid_state;
    batch = dc_batch_;
    dc_batch_ = {};
    return Status::ok;
}
void EspMainsInput::stop() noexcept {
    if (running_) { checked(adc_continuous_stop(handle_)); running_ = false; }
    dc_batch_ = {};
}
void EspMainsInput::deinit() noexcept {
    stop();
    if (handle_) { checked(adc_continuous_deinit(handle_)); handle_ = nullptr; }
#if CONFIG_IDF_TARGET_ESP32C6
    if (dc_calibration_) {
        checked(adc_cali_delete_scheme_curve_fitting(dc_calibration_));
        dc_calibration_ = nullptr;
    }
#endif
}
void EspMainsSink::report(const MainsReading& r) noexcept {
    if (!r.valid) {
        ESP_LOGW("mains", "INVALID: samples=%u min=%u max=%u (gap, clipping or ADC overflow)",
                 r.samples, r.minimum, r.maximum);
    } else if (r.calibrated) {
        ESP_LOGI("mains", "U=%.1f V RMS | adc_rms=%.2f mean=%.1f min=%u max=%u n=%u",
                 r.volts, r.rms, r.mean, r.minimum, r.maximum, r.samples);
    } else {
        ESP_LOGI("mains", "UNCALIBRATED | adc_rms=%.2f counts mean=%.1f min=%u max=%u n=%u",
                 r.rms, r.mean, r.minimum, r.maximum, r.samples);
    }
}
}  // namespace bth
