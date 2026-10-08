#pragma once
#include "bth/core/module.hpp"
#include "bth/relay/relay.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
namespace bth {
class RelayService final : public Module {
public:
    RelayService(RelayController& controller, bool configured) noexcept
        : controller_(controller), configured_(configured) {}
    const char* id() const noexcept override { return "relay"; }
    Dependencies dependencies() const noexcept override { return {required_, 1}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t) noexcept override { return Status::ok; }
    void stop() noexcept override;
    void deinit() noexcept override;
    RelayState snapshot() const noexcept;
    // Called only from the single HTTP task. Never touches Runtime.
    bool request(bool on, RelayState& result) noexcept;
    // Nonblocking producer for Zigbee. The same worker owns GPIO and NVS.
    bool post(bool on) noexcept;
    bool idle() const noexcept { return pending_.load() == 0; }
private:
    struct Command { std::uint32_t id; bool on; bool stop; };
    struct Reply { std::uint32_t id; RelayState state; };
    inline static constexpr const char* required_[] = {"nvs"};
    static void worker(void* context);
    RelayController& controller_;
    const bool configured_;
    QueueHandle_t commands_ = nullptr;
    QueueHandle_t replies_ = nullptr;
    QueueHandle_t states_ = nullptr;
    SemaphoreHandle_t joined_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::uint32_t next_id_ = 0;
    std::atomic<unsigned> pending_{0};
};
}  // namespace bth
