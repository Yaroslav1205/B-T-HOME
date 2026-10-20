#pragma once

#include "bth/oled/oled.hpp"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>

namespace bth {
struct OledI2cConfig {
    int port;
    int sda;
    int scl;
    std::uint32_t speed_hz;
    std::uint16_t address;
    bool internal_pullups;
};

class EspOledI2c final : public OledTransport, private OledIo {
public:
    explicit EspOledI2c(OledI2cConfig config) noexcept : config_(config) {}
    EspOledI2c(const EspOledI2c&) = delete;
    EspOledI2c& operator=(const EspOledI2c&) = delete;
    Status init() noexcept override;
    Status start_updates() noexcept override;
    Status submit(const OledUpdate& update) noexcept override;
    Status poll() noexcept override { return update_status_.load(); }
    void stop_updates() noexcept override;
    void deinit() noexcept override;
private:
    Status write(const std::uint8_t* bytes, std::size_t size) noexcept override;
    Status recover_bus() noexcept override;
    struct Update { OledUpdate display{}; bool stop = false; };
    static void worker(void* context);
    const OledI2cConfig config_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    QueueHandle_t updates_ = nullptr;
    SemaphoreHandle_t joined_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<Status> update_status_{Status::ok};
};
}  // namespace bth
