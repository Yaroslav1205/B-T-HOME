#pragma once
#include "bth/core/module.hpp"
#include "bth/ota/update.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
namespace bth {
struct OtaSnapshot {
    char state[24] = "idle";
    char error[48]{};
    char version[32]{};
    std::size_t received = 0, total = 0;
};
// Lifecycle owned by RelayWeb; no additional Runtime slot.
class OtaService final : private ota::Observer {
public:
    Status init() noexcept;
    Status start() noexcept;
    void stop() noexcept;
    void deinit() noexcept;
    void connected(bool value) noexcept { connected_.store(value); }
    bool connected() const noexcept { return connected_.load(); }
    bool request(bool install) noexcept;
    bool busy() const noexcept { return busy_.load(); }
    bool reboot_requested() const noexcept { return reboot_.load(); }
    OtaSnapshot snapshot() const noexcept;
    static bool supported() noexcept;
    static bool confirm_boot() noexcept;
private:
    static void run(void* context);
    void work() noexcept;
    bool check(ota::Release& release) noexcept;
    void publish(const char* state, const char* error = "") noexcept;
    bool cancelled() const noexcept override;
    void progress(std::size_t bytes) noexcept override;
    QueueHandle_t commands_ = nullptr, snapshots_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> stopping_{true}, connected_{false}, busy_{false}, reboot_{false};
    OtaSnapshot current_{};
    std::int64_t deadline_ = 0;
};
}  // namespace bth
