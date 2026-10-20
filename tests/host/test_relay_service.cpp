#include "bth/platform/relay_service.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
#define CHECK_RS(c) do { if (!(c)) { std::cerr << "relay service:" << __LINE__ << ": " #c "\n"; std::_Exit(EXIT_FAILURE); } } while(false)
struct TestQueue {
    unsigned capacity, size;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<unsigned char>> items;
    TestQueue(unsigned cap, unsigned bytes) : capacity(cap), size(bytes) {}
};
namespace {
std::vector<std::thread> workers;
bool fail_task = false;
unsigned queues = 0;
int fail_queue_after = -1;
template<class Predicate>
bool await_queue(TestQueue& q, std::unique_lock<std::mutex>& lock, TickType_t wait, Predicate ready) {
    if (wait == portMAX_DELAY) { q.changed.wait(lock, ready); return true; }
    return q.changed.wait_for(lock, std::chrono::milliseconds(wait), ready);
}
template<class Predicate> void eventually(Predicate ready) {
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!ready()) {
        CHECK_RS(std::chrono::steady_clock::now() < until);
        std::this_thread::sleep_for(1ms);
    }
}
void join_workers() { for (auto& t : workers) t.join(); workers.clear(); }
struct Output final : bth::RelayOutput {
    std::atomic<bool> blocked{false}, entered{false}, on{false};
    bth::Status init() noexcept override { on = false; return bth::Status::ok; }
    bth::Status set(bool value) noexcept override {
        entered = true;
        while (blocked) std::this_thread::sleep_for(1ms);
        on = value; return bth::Status::ok;
    }
    void deinit() noexcept override { on = false; }
};
struct Store final : bth::RelayStore {
    bool saved = false;
    bth::RelayMode mode = bth::RelayMode::manual;
    std::atomic<bool> fail_save{false};
    bth::Status init() noexcept override { return bth::Status::ok; }
    bth::Status load(bool& on) noexcept override { on = saved; return bth::Status::ok; }
    bth::Status save(bool on) noexcept override {
        if (fail_save) return bth::Status::hardware_error;
        saved = on; return bth::Status::ok;
    }
    bth::Status load_mode(bth::RelayMode& value) noexcept override { value = mode; return bth::Status::ok; }
    bth::Status save_mode(bth::RelayMode value) noexcept override { mode = value; return bth::Status::ok; }
    void deinit() noexcept override {}
};
}
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t size) {
    if (fail_queue_after == 0) return nullptr;
    if (fail_queue_after > 0) --fail_queue_after;
    ++queues;
    return new TestQueue(capacity,size);
}
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if (!await_queue(*q, lock, wait, [&]{ return q->items.size() < q->capacity; })) return pdFALSE;
    const auto* bytes = static_cast<const unsigned char*>(item);
    q->items.emplace_back(bytes, bytes + q->size); q->changed.notify_all(); return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if (!await_queue(*q, lock, wait, [&]{ return !q->items.empty(); })) return pdFALSE;
    std::memcpy(item, q->items.front().data(), q->size); q->items.pop_front(); q->changed.notify_all(); return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if (!await_queue(*q, lock, wait, [&]{ return !q->items.empty(); })) return pdFALSE;
    std::memcpy(item, q->items.front().data(), q->size); return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item) {
    std::lock_guard<std::mutex> lock(q->mutex);
    CHECK_RS(q->capacity == 1);
    const auto* bytes = static_cast<const unsigned char*>(item);
    q->items.clear(); q->items.emplace_back(bytes, bytes+q->size); q->changed.notify_all(); return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { delete q; --queues; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return xQueueCreate(1,1); }
BaseType_t xSemaphoreGive(SemaphoreHandle_t q) { unsigned char token=1; return xQueueSend(q,&token,0); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t q, TickType_t wait) { unsigned char token; return xQueueReceive(q,&token,wait); }
void vSemaphoreDelete(SemaphoreHandle_t q) { vQueueDelete(q); }
BaseType_t xTaskCreate(void (*worker)(void*), const char*, unsigned, void* context, unsigned, TaskHandle_t* handle) {
    if (fail_task) return pdFALSE;
    *handle = context; workers.emplace_back(worker,context); return pdPASS;
}
TickType_t xTaskGetTickCount() {
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
void vTaskDelete(TaskHandle_t) {}

void test_relay_service() {
    Output output; Store store;
    bth::RelayController controller(output,store);
    bth::RelayService service(controller,true);
    CHECK_RS(service.start() == bth::Status::invalid_state);
    for (int allocation = 0; allocation < 4; ++allocation) {
        fail_queue_after = allocation;
        CHECK_RS(service.init() == bth::Status::no_memory);
        CHECK_RS(service.start() == bth::Status::invalid_state);
        service.deinit(); service.deinit();
        CHECK_RS(queues == 0);
    }
    fail_queue_after = -1;
    CHECK_RS(service.init() == bth::Status::ok && service.start() == bth::Status::ok);
    CHECK_RS(service.init() == bth::Status::invalid_state && queues == 4);
    CHECK_RS(service.start() == bth::Status::invalid_state && workers.size() == 1);
    output.entered = false; output.blocked = true;
    CHECK_RS(service.post(true));
    eventually([&]{return output.entered.load();});
    CHECK_RS(!service.idle() && !service.snapshot().on);
    for (int i=0;i<4;++i) CHECK_RS(service.post(i%2 != 0));
    CHECK_RS(!service.post(false)); // Full queue rejects without blocking or corrupting pending count.
    output.blocked = false;
    eventually([&]{return service.idle();});
    CHECK_RS(service.snapshot().on && service.snapshot().saved_on);
    // Fire-and-forget replies must not consume the HTTP client's response capacity.
    for (int i=0;i<12;++i) {
        CHECK_RS(service.post(i%2 != 0));
        eventually([&]{return service.idle();});
    }
    bth::RelayState result;
    CHECK_RS(service.request(false,result) && result.ready && !result.on && !result.saved_on);
    CHECK_RS(service.post(true));
    service.stop(); // Drains accepted commands and joins before releasing resources.
    CHECK_RS(!service.snapshot().ready && !service.snapshot().on && service.snapshot().saved_on);
    CHECK_RS(!service.post(false) && service.idle());
    CHECK_RS(service.request(false, result) && !result.ready);
    service.stop();
    CHECK_RS(service.start() == bth::Status::ok && service.snapshot().on);
    CHECK_RS(service.request(false, result) && result.ready && !result.on);
    CHECK_RS(service.request(true, result) && result.ready && result.on);
    service.deinit(); service.deinit(); join_workers();
    CHECK_RS(queues == 0 && !output.on && store.saved);
    CHECK_RS(service.init() == bth::Status::ok);
    fail_task = true;
    CHECK_RS(service.start() == bth::Status::no_memory);
    CHECK_RS(!service.snapshot().ready && !service.post(false) && service.idle());
    service.stop(); service.deinit(); fail_task = false;
    CHECK_RS(queues == 0 && !output.on);
    CHECK_RS(service.init() == bth::Status::ok && service.start() == bth::Status::ok);
    CHECK_RS(service.snapshot().on && service.idle());
    store.fail_save = true;
    CHECK_RS(service.post(false));
    eventually([&]{return service.idle();});
    CHECK_RS(!service.snapshot().ready && service.snapshot().on && !service.snapshot().storage_known);
    CHECK_RS(!service.post(false));
    service.deinit(); join_workers();
    CHECK_RS(queues == 0);
    store.fail_save = false;
    CHECK_RS(service.init() == bth::Status::ok && service.start() == bth::Status::ok);
    CHECK_RS(service.request_mode(bth::RelayMode::outage, result) && !result.on);
    bth::MainsReading reading{};
    reading.valid = reading.calibrated = true;
    reading.history.current.known = true;
    reading.history.current.present = false;
    service.report(reading);service.tick(1000);
    eventually([&]{return service.snapshot().on;});
    CHECK_RS(!service.post(false));
    CHECK_RS(service.request(false, result) && result.on);
    service.tick(3500);
    eventually([&]{return !service.snapshot().on;});
    service.report(reading);service.tick(4000);
    eventually([&]{return service.snapshot().on;});
    reading.valid = false;service.report(reading);service.tick(4100);
    eventually([&]{return !service.snapshot().on;});
    reading.valid = true;reading.history.current.present = true;
    service.report(reading);service.tick(4200);
    CHECK_RS(service.request_mode(bth::RelayMode::unused, result) && !result.on);
    CHECK_RS(!service.post(true));
    CHECK_RS(service.request_mode(bth::RelayMode::manual, result) && result.on);
    service.deinit();join_workers();CHECK_RS(queues == 0);
    bth::RelayService unavailable(controller,false);
    CHECK_RS(unavailable.start() == bth::Status::invalid_state);
    CHECK_RS(unavailable.init() == bth::Status::ok && unavailable.start() == bth::Status::ok);
    CHECK_RS(unavailable.start() == bth::Status::invalid_state);
    CHECK_RS(!unavailable.post(true));
    unavailable.deinit(); CHECK_RS(queues == 0);
}
