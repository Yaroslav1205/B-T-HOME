#pragma once
#include "bth/core/module.hpp"
#include "bth/platform/relay_service.hpp"
#include "bth/zigbee/control.hpp"
#include "bth/zigbee/storage.hpp"
#include <atomic>

namespace bth {
class ZigbeeModule final : public Module, public ZigbeeControl {
public:
    explicit ZigbeeModule(RelayService& relay) noexcept : relay_(relay) {}
    const char* id() const noexcept override { return "zigbee"; }
    Dependencies dependencies() const noexcept override { return {required_, 2}; }
    Status init() noexcept override;
    Status start() noexcept override;
    Status tick(std::uint64_t now) noexcept override;
    void stop() noexcept override;
    void deinit() noexcept override;
    ZigbeeSnapshot snapshot() const noexcept override;
    bool request(ZigbeeCommand command) noexcept override;
private:
    inline static constexpr const char* required_[] = {"nvs", "relay"};
    // SDK callbacks have no context argument. This is the sole radio adapter owner.
    static ZigbeeModule* owner_;
    static bool signal(void* const* app_signal);
    static void action(std::uint32_t id, void* message);
    static void worker(void* context);
    bool setup_endpoint() noexcept;
    void publish(ZigbeeState state, std::uint64_t now) noexcept;
    RelayService& relay_;
    ZigbeeStorage storage_;
    QueueHandle_t commands_ = nullptr, states_ = nullptr;
    SemaphoreHandle_t joined_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> worker_failed_{false};
    std::atomic<bool> accepting_{false};
    void* descriptor_ = nullptr;
    bool initialized_ = false, started_ = false, callbacks_ = false;
    bool network_ready_ = false, resetting_ = false, local_update_ = false;
    bool cancel_sent_ = false;
    std::uint64_t retry_init_ms_ = 0, next_tick_ms_ = 0;
    ZigbeeJoinWindow join_{};
};
}  // namespace bth
