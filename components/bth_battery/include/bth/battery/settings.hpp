#pragma once
#include "bth/core/module.hpp"

namespace bth {
inline constexpr std::uint32_t battery_default_discharge_ma = 58;
inline constexpr std::uint32_t battery_max_discharge_ma = 100000;

// Reads return a synchronized snapshot; updates belong to the HTTP owner task.
class BatterySettings {
public:
    virtual ~BatterySettings() = default;
    virtual std::uint32_t discharge_ma() const noexcept = 0;
    virtual Status update_discharge(std::uint32_t milliamps) noexcept = 0;
};
}  // namespace bth
