#include "bth/platform/relay_service.hpp"
namespace bth {
Status RelayService::init() noexcept {
    pending_ = 0;
    commands_ = xQueueCreate(4, sizeof(Command));
    replies_ = xQueueCreate(4, sizeof(Reply));
    states_ = xQueueCreate(1, sizeof(RelayState));
    joined_ = xSemaphoreCreateBinary();
    if (!commands_ || !replies_ || !states_ || !joined_) { return Status::no_memory; }
    if (configured_) { return controller_.init(); }
    const RelayState unavailable{};
    xQueueOverwrite(states_, &unavailable);
    return Status::ok;
}
Status RelayService::start() noexcept {
    if (!configured_) { return Status::ok; }
    const auto result = controller_.start();
    if (result != Status::ok) { return result; }
    const auto state = controller_.state();
    xQueueOverwrite(states_, &state);
    if (xTaskCreate(worker, "relay", 4096, this, 4, &task_) != pdPASS) {
        return Status::no_memory;
    }
    return Status::ok;
}
void RelayService::worker(void* context) {
    auto& self = *static_cast<RelayService*>(context);
    Command command{};
    while (xQueueReceive(self.commands_, &command, portMAX_DELAY) == pdTRUE) {
        if (command.stop) { break; }
        (void)self.controller_.set(command.on);
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
bool RelayService::request(bool on, RelayState& result) noexcept {
    if (!configured_ || !snapshot().ready) { result = snapshot(); return true; }
    Reply reply{};
    while (xQueueReceive(replies_, &reply, 0) == pdTRUE) {}
    if (++next_id_ == 0) ++next_id_;
    const Command command{next_id_, on, false};
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
    if (!commands_ || !configured_ || !snapshot().ready) return false;
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
}
void RelayService::deinit() noexcept {
    stop();
    controller_.deinit();
    if (commands_) { vQueueDelete(commands_); commands_ = nullptr; }
    if (replies_) { vQueueDelete(replies_); replies_ = nullptr; }
    if (states_) { vQueueDelete(states_); states_ = nullptr; }
    if (joined_) { vSemaphoreDelete(joined_); joined_ = nullptr; }
}
}  // namespace bth
