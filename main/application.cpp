#include "application.hpp"

#include "bth/board/config.hpp"
#include "bth/core/runtime.hpp"
#include "bth/heartbeat/heartbeat.hpp"
#include "bth/platform/esp_heartbeat_sink.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace bth::app {
namespace {
constexpr const char* tag = "app";

void log_failure(const Runtime& runtime) {
    const auto& failure = runtime.failure();
    ESP_LOGE(tag, "module=%s phase=%s error=%s dependency=%s",
             failure.module ? failure.module : "-", phase_name(failure.phase),
             status_name(failure.status), failure.dependency ? failure.dependency : "-");
}
}  // namespace

void run() {
    ESP_LOGI(tag, "B-T HOME | board=%s | target=%s", board::id, board::target);

    // Composition root: dependencies first, then consumers. No hidden registration.
    // Declare modules BEFORE Runtime: reverse destruction keeps references valid.
#if CONFIG_BTH_HEARTBEAT_ENABLED
    EspHeartbeatSink heartbeat_sink;
    Heartbeat heartbeat(heartbeat_sink, CONFIG_BTH_HEARTBEAT_INTERVAL_MS);
#endif
    Runtime runtime;
#if CONFIG_BTH_HEARTBEAT_ENABLED
    if (runtime.add(heartbeat) != Status::ok) {
        log_failure(runtime);
        return;
    }
#endif
    if (runtime.start() != Status::ok) {
        log_failure(runtime);
        return;
    }
    ESP_LOGI(tag, "Started %u module(s)", static_cast<unsigned>(runtime.size()));

    const TickType_t delay_ticks = pdMS_TO_TICKS(CONFIG_BTH_LOOP_INTERVAL_MS);
    for (;;) {
        const auto now_ms = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
        if (runtime.tick(now_ms) != Status::ok) {
            log_failure(runtime);
            // Runtime has already stopped and deinitialized all modules.
            return;
        }
        vTaskDelay(delay_ticks > 0 ? delay_ticks : 1);
    }
}

}  // namespace bth::app
