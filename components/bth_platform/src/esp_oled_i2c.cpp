#include "bth/platform/esp_oled_i2c.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include <algorithm>

namespace bth {
namespace {
Status checked(esp_err_t result, const char* operation) noexcept {
    if (result == ESP_OK) { return Status::ok; }
    ESP_LOGE("oled", "%s: %s", operation, esp_err_to_name(result));
    return result == ESP_ERR_NO_MEM ? Status::no_memory : Status::hardware_error;
}
}  // namespace

Status EspOledI2c::init() noexcept {
    if (bus_ || device_ || updates_ || joined_) { return Status::invalid_state; }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config_.sda) || !GPIO_IS_VALID_OUTPUT_GPIO(config_.scl) ||
        config_.sda == config_.scl || config_.address > 0x7f || config_.speed_hz == 0) {
        return Status::invalid_argument;
    }
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = config_.port;
    bus_config.sda_io_num = static_cast<gpio_num_t>(config_.sda);
    bus_config.scl_io_num = static_cast<gpio_num_t>(config_.scl);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = config_.internal_pullups;
    auto result = checked(i2c_new_master_bus(&bus_config, &bus_), "create bus");
    if (result != Status::ok) { return result; }
    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = config_.address;
    device_config.scl_speed_hz = config_.speed_hz;
    result = checked(i2c_master_bus_add_device(bus_, &device_config, &device_), "add SSD1306");
    if (result != Status::ok) return result;
    updates_ = xQueueCreate(1, sizeof(Update));
    joined_ = xSemaphoreCreateBinary();
    return updates_ && joined_ ? Status::ok : Status::no_memory;
}

Status EspOledI2c::probe(bool& present) noexcept {
    present = false;
    if (!bus_ || !device_) { return Status::invalid_state; }
    const auto result = i2c_master_probe(bus_, config_.address, 100);
    if (result == ESP_ERR_NOT_FOUND || result == ESP_ERR_TIMEOUT) {
        ESP_LOGW("oled", "No response at 0x%02x; continuing without OLED (%s)",
                 static_cast<unsigned>(config_.address), esp_err_to_name(result));
        return Status::ok;
    }
    if (result != ESP_OK) { return checked(result, "probe SSD1306"); }
    present = true;
    ESP_LOGI("oled", "Display detected at 0x%02x", static_cast<unsigned>(config_.address));
    return Status::ok;
}

Status EspOledI2c::write(const std::uint8_t* bytes, std::size_t size) noexcept {
    if (!device_) { return Status::invalid_state; }
    if (!bytes || size == 0) { return Status::invalid_argument; }
    // A frame needs about 47 ms at 100 kHz: lifecycle or worker only, never tick.
    return checked(i2c_master_transmit(device_, bytes, size, 100), "write SSD1306");
}

Status EspOledI2c::start_updates() noexcept {
    if (!device_ || !updates_ || !joined_ || task_) return Status::invalid_state;
    xQueueReset(updates_);
    update_status_.store(Status::ok);
    return xTaskCreate(worker, "oled", 3072, this, 2, &task_) == pdPASS
        ? Status::ok : Status::no_memory;
}

Status EspOledI2c::submit(const OledFrame& frame) noexcept {
    if (!task_) return Status::invalid_state;
    const auto status = poll();
    if (status != Status::ok) return status;
    const Update update{frame, false};
    return xQueueOverwrite(updates_, &update) == pdPASS ? Status::ok : Status::internal_error;
}

void EspOledI2c::worker(void* context) {
    auto& self = *static_cast<EspOledI2c*>(context);
    Update update;
    while (xQueueReceive(self.updates_, &update, portMAX_DELAY) == pdTRUE) {
        if (update.stop) break;
        if (self.poll() != Status::ok) continue;
        // Reset the address window for every complete frame.
        const std::uint8_t window[] = {0x00, 0x21, 0x00, 0x7f, 0x22, 0x00, 0x03};
        auto status = self.write(window, sizeof(window));
        if (status == Status::ok) {
            std::array<std::uint8_t, 513> data{};
            data[0] = 0x40;
            std::copy(update.frame.begin(), update.frame.end(), data.begin() + 1);
            status = self.write(data.data(), data.size());
        }
        self.update_status_.store(status);
    }
    xSemaphoreGive(self.joined_);
    vTaskDelete(nullptr);
}

void EspOledI2c::stop_updates() noexcept {
    if (!task_) return;
    const Update stop{{}, true};
    xQueueOverwrite(updates_, &stop);
    xSemaphoreTake(joined_, portMAX_DELAY);
    task_ = nullptr;
}

void EspOledI2c::deinit() noexcept {
    stop_updates();
    if (updates_) { vQueueDelete(updates_); updates_ = nullptr; }
    if (joined_) { vSemaphoreDelete(joined_); joined_ = nullptr; }
    if (device_) {
        if (checked(i2c_master_bus_rm_device(device_), "remove SSD1306") != Status::ok) { return; }
        device_ = nullptr;
    }
    if (bus_) {
        if (checked(i2c_del_master_bus(bus_), "delete bus") != Status::ok) { return; }
        bus_ = nullptr;
    }
}
}  // namespace bth
