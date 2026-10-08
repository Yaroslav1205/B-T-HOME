#pragma once
#include "bth/mains/mains.hpp"
#include "bth/dc/dc.hpp"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include <atomic>

namespace bth {
class EspMainsInput final : public MainsInput, public DcInput {
public:
    explicit EspMainsInput(int gpio, int dc_gpio = -1) noexcept : gpio_(gpio), dc_gpio_(dc_gpio) {}
    EspMainsInput(const EspMainsInput&) = delete;
    EspMainsInput& operator=(const EspMainsInput&) = delete;
    Status init() noexcept override;
    Status start() noexcept override;
    Status read(std::uint16_t*, std::size_t, std::size_t&, bool&) noexcept override;
    Status read_dc(DcBatch& batch) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    static bool overflow(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void* context);
    int gpio_;
    int dc_gpio_;
    unsigned mains_channel_ = 0, dc_channel_ = 0;
    adc_cali_handle_t dc_calibration_ = nullptr;
    DcBatch dc_batch_{};
    adc_continuous_handle_t handle_ = nullptr;
    bool running_ = false;
    std::atomic<bool> overflow_{false};
};
class EspMainsSink final : public MainsSink {
public:
    void report(const MainsReading&) noexcept override;
};
}  // namespace bth
