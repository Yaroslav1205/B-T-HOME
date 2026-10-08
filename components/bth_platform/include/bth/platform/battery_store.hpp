#pragma once
#include "bth/battery/battery.hpp"
#include "nvs.h"
namespace bth {
class NvsBatteryStore final : public BatteryStore {
public:
    Status init() noexcept override;
    Status load(std::uint64_t& charge_ma_ms, bool& found) noexcept override;
    Status save(std::uint64_t charge_ma_ms) noexcept override;
    void deinit() noexcept override;
private:
    nvs_handle_t handle_ = 0;
};
} // namespace bth
