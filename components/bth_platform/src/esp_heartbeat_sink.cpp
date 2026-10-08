#include "bth/platform/esp_heartbeat_sink.hpp"

#include <cinttypes>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace bth {

void EspHeartbeatSink::report(std::uint64_t uptime_ms) noexcept {
    ESP_LOGI("heartbeat", "uptime=%" PRIu64 " ms, free_heap=%" PRIu32
             " bytes, min_free_heap=%" PRIu32 " bytes, main_stack_min_free=%u bytes",
             uptime_ms, esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

}  // namespace bth
