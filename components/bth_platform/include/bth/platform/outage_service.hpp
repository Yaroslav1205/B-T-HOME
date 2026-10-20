#pragma once
#include "bth/outages/outages.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>

namespace bth {
class OutageService final : public Module {
public:
    OutageService(OutageSettings& settings, Dependencies providers) noexcept : settings_(settings), providers_(providers) {}
    const char* id() const noexcept override { return "outages"; }
    Dependencies dependencies() const noexcept override { return providers_; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    // Runtime owner supplies connection state; the worker never touches WifiAp.
    void connected(bool value) noexcept { connected_.store(value); }
    OutageDisplay display() const noexcept;
    void stop() noexcept override;
    void deinit() noexcept override;
private:
    struct Snapshot { OutageSchedule schedule{}; std::uint64_t at = 0; };
    static void run(void* context);
    void work() noexcept;
    bool fetch(unsigned group, unsigned date, OutageSchedule& result) noexcept;
    OutageSettings& settings_;
    Dependencies providers_;
    QueueHandle_t results_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> stopping_{false}, connected_{false};
};
}  // namespace bth
