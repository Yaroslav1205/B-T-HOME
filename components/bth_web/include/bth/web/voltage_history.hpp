#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace bth {
// Caller serializes access. Fixed storage, no allocation or wall-clock dependency.
class VoltageHistory {
public:
    static constexpr std::uint32_t interval_seconds = 300;
    static constexpr std::size_t capacity = 7 * 24 * 12 + 1;
    struct Point {
        std::uint32_t bucket = 0;
        std::uint32_t sum = 0;
        std::uint16_t minimum = 0, maximum = 0, count = 0;
    };
    void record(std::uint64_t now_ms, double volts, bool valid) noexcept {
        const auto second = now_ms / 1000;
        if (received_ && second <= last_second_) return;
        received_ = true;
        last_second_ = second;
        const auto bucket = static_cast<std::uint32_t>(second / interval_seconds);
        auto& point = points_[bucket % capacity];
        if (point.bucket != bucket) point = {bucket, 0, 0, 0, 0};
        if (!valid || !std::isfinite(volts) || volts < 0 || volts >= 999.95) return;
        const auto value = static_cast<std::uint16_t>(std::lround(volts * 10));
        if (!point.count || value < point.minimum) point.minimum = value;
        if (!point.count || value > point.maximum) point.maximum = value;
        point.sum += value;
        ++point.count;
    }
    Point get(std::uint32_t bucket) const noexcept {
        const auto& point = points_[bucket % capacity];
        return point.bucket == bucket ? point : Point{bucket, 0, 0, 0, 0};
    }
private:
    std::array<Point, capacity> points_{};
    std::uint64_t last_second_ = 0;
    bool received_ = false;
};
}  // namespace bth
