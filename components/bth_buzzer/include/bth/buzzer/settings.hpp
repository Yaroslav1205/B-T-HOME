#pragma once
#include "bth/core/module.hpp"

namespace bth {
inline constexpr std::uint32_t buzzer_default_interval_seconds = 20;
inline constexpr std::uint32_t buzzer_max_interval_seconds = 86400;

// Reads return a synchronized snapshot; updates belong to the HTTP owner task.
class BuzzerSettings {
public:
    virtual ~BuzzerSettings() = default;
    virtual std::uint32_t interval_seconds() const noexcept = 0;
    virtual Status update_interval(std::uint32_t seconds) noexcept = 0;
};
}  // namespace bth
