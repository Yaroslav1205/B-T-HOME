#include "bth/platform/relay_service.hpp"
#include <cmath>
namespace bth {
Status RelayService::init() noexcept {
    if (commands_ || replies_ || states_ || joined_) return Status::invalid_state;
    pending_ = 0;
    absent_ = false; fresh_ = reported_absent_ = false; report_ms_ = 0;
    commands_ = xQueueCreate(4, sizeof(Command));
    replies_ = xQueueCreate(4, sizeof(Reply));
    states_ = xQueueCreate(1, sizeof(RelayState));
    joined_ = xSemaphoreCreateBinary();
    if (!commands_ || !replies_ || !states_ || !joined_) { return Status::no_memory; }
    if (configured_) {
        const auto result = controller_.init();
        initialized_ = result == Status::ok;
        return result;
    }
    const RelayState unavailable{};
    xQueueOverwrite(states_, &unavailable);
    initialized_ = true;
    return Status::ok;
}
Status RelayService::start() noexcept {
    if (!initialized_ || running_) return Status::invalid_state;
    if (!configured_) { running_ = true; return Status::ok; }
    absent_ = false; fresh_ = reported_absent_ = false; report_ms_ = 0;
    const auto result = controller_.start();
    if (result != Status::ok) { return result; }
    if (xTaskCreate(worker, "relay", 4096, this, 4, &task_) != pdPASS) {
        return Status::no_memory;
    }
    // Publish readiness only after a worker exists to consume commands.
    const auto state = controller_.state();
    xQueueOverwrite(states_, &state);
    running_ = true;
    return Status::ok;
}
void RelayService::worker(void* context) {
    auto& self = *static_cast<RelayService*>(context);
    Command command{};
    for (;;) {
        if (xQueueReceive(self.commands_, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
            (void)self.controller_.automatic(self.absent_.load());
            const auto state = self.controller_.state();
            xQueueOverwrite(self.states_, &state);
            continue;
        }
        if (command.stop) { break; }
        if (command.mode >= 0)
            (void)self.controller_.set_mode(static_cast<RelayMode>(command.mode), self.absent_.load());
        else (void)self.controller_.set(command.on);
        (void)self.controller_.automatic(self.absent_.load());
        const auto state = self.controller_.state();
        xQueueOverwrite(self.states_, &state);
        --self.pending_;
        const Reply reply{command.id, state};
        // A client may have timed out. Never block the owner on an abandoned reply.
        if (command.id != 0) (void)xQueueSend(self.replies_, &reply, 0);
    }
    xSemaphoreGive(self.joined_);
    vTaskDelete(nullptr);
}
RelayState RelayService::snapshot() const noexcept {
    RelayState state{};
    if (states_) { (void)xQueuePeek(states_, &state, 0); }
    return state;
}
void RelayService::report(const MainsReading& reading) noexcept {
    reported_absent_ = reading.valid && reading.calibrated && std::isfinite(reading.volts) &&
        reading.volts >= 0 && reading.history.current.known && !reading.history.current.present;
    fresh_ = true;
}
Status RelayService::tick(std::uint64_t now_ms) noexcept {
    if (fresh_) { report_ms_ = now_ms; fresh_ = false; }
    absent_ = reported_absent_ && now_ms >= report_ms_ && now_ms - report_ms_ < 2500;
    return Status::ok;
}
bool RelayService::request_mode(RelayMode mode, RelayState& result) noexcept {
    if (static_cast<unsigned>(mode) > 2) return false;
    return exchange({0, false, false, static_cast<int>(mode)}, result);
}
bool RelayService::request(bool on, RelayState& result) noexcept {
    if (snapshot().mode != RelayMode::manual) { result = snapshot(); return true; }
    return exchange({0, on, false}, result);
}
bool RelayService::exchange(Command command, RelayState& result) noexcept {
    if (!configured_ || !snapshot().ready) { result = snapshot(); return true; }
    Reply reply{};
    while (xQueueReceive(replies_, &reply, 0) == pdTRUE) {}
    if (++next_id_ == 0) ++next_id_;
    command.id = next_id_;
    ++pending_;
    if (xQueueSend(commands_, &command, 0) != pdTRUE) { --pending_; return false; }
    const auto began = xTaskGetTickCount();
    const auto timeout = pdMS_TO_TICKS(3000);
    for (;;) {
        const auto elapsed = xTaskGetTickCount() - began;
        if (elapsed >= timeout) { return false; }
        if (xQueueReceive(replies_, &reply, timeout - elapsed) != pdTRUE) { return false; }
        if (reply.id == command.id) { result = reply.state; return true; }
    }
}
bool RelayService::post(bool on) noexcept {
    if (!commands_ || !configured_ || !snapshot().ready || snapshot().mode != RelayMode::manual) return false;
    const Command command{0, on, false};
    ++pending_;
    if (xQueueSend(commands_, &command, 0) == pdTRUE) return true;
    --pending_;
    return false;
}
void RelayService::stop() noexcept {
    // Web and Zigbee stop first, so no new commands can race with this join.
    if (task_) {
        const Command command{0, false, true};
        xQueueSend(commands_, &command, portMAX_DELAY);
        xSemaphoreTake(joined_, portMAX_DELAY);
        task_ = nullptr;
    }
    controller_.stop();
    running_ = false;
    if (states_) {
        const auto state = configured_ ? controller_.state() : RelayState{};
        xQueueOverwrite(states_, &state);
    }
}
void RelayService::deinit() noexcept {
    stop();
    controller_.deinit();
    if (commands_) { vQueueDelete(commands_); commands_ = nullptr; }
    if (replies_) { vQueueDelete(replies_); replies_ = nullptr; }
    if (states_) { vQueueDelete(states_); states_ = nullptr; }
    if (joined_) { vSemaphoreDelete(joined_); joined_ = nullptr; }
    initialized_ = false;
}
}  // namespace bth
