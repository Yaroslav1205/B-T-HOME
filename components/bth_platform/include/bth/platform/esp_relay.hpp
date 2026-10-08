#pragma once
#include "bth/relay/relay.hpp"
namespace bth {
class EspRelay final : public RelayOutput {
public:
    EspRelay(int gpio, bool active_high) noexcept : gpio_(gpio), active_high_(active_high) {}
    Status init() noexcept override;
    Status set(bool on) noexcept override;
    void deinit() noexcept override;
private:
    int gpio_;
    bool active_high_;
    bool acquired_ = false;
};
}  // namespace bth
