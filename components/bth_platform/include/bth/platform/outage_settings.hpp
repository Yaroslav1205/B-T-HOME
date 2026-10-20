#pragma once
#include "bth/outages/outages.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"

namespace bth {
// NVS writes run on HTTP; Runtime reads only the one-item queue.
// Register before web/radio and the outage worker so both join before deinit.
class NvsOutageSettings final : public Module, public OutageSettings {
public:
    const char* id() const noexcept override { return "outage_settings"; }
    Dependencies dependencies() const noexcept override { return {providers_, 1}; }
    Status init() noexcept override;
    Status start() noexcept override { return queue_ && writable_ ? Status::ok : Status::invalid_state; }
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override {}
    void deinit() noexcept override;
    std::uint32_t group() const noexcept override;
    Status update_group(std::uint32_t selection) noexcept override;
private:
    inline static constexpr const char* providers_[] = {"nvs"};
    nvs_handle_t handle_ = 0;
    QueueHandle_t queue_ = nullptr;
    bool writable_ = false;
};
}  // namespace bth
