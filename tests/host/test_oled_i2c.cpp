#include "bth/platform/esp_oled_i2c.hpp"
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
#define CHECK(c) do { if (!(c)) { std::cerr << "oled i2c:" << __LINE__ << ": " #c "\n"; std::_Exit(1); } } while(false)
struct TestQueue {
    unsigned size;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<unsigned char>> items;
    explicit TestQueue(unsigned bytes) : size(bytes) {}
};
namespace {
std::vector<std::thread> workers;
std::atomic<unsigned> calls{0}, resets{0}, setups{0}, on_commands{0};
std::atomic<bool> blocked{false}, entered{false}, offline{false}, in_transfer{false};
bool bus=false, device=false, fail_bus=false, fail_device=false, fail_task=false;
int fail_queue=-1;
unsigned queues=0;
std::thread::id owner;
template<class Predicate> void eventually(Predicate predicate) {
    const auto limit=std::chrono::steady_clock::now()+2s;
    while (!predicate()) { CHECK(std::chrono::steady_clock::now()<limit); std::this_thread::sleep_for(1ms); }
}
void join_workers() { for(auto& t:workers)t.join(); workers.clear(); }
}
const char* esp_err_to_name(esp_err_t) { return "test error"; }
QueueHandle_t xQueueCreate(UBaseType_t capacity,UBaseType_t size) {
    CHECK(capacity==1);
    if(fail_queue==0)return nullptr;
    if(fail_queue>0)--fail_queue;
    ++queues; return new TestQueue(size);
}
BaseType_t xQueueOverwrite(QueueHandle_t q,const void* value) {
    std::lock_guard<std::mutex> lock(q->mutex);
    const auto* bytes=static_cast<const unsigned char*>(value);
    q->items.clear(); q->items.emplace_back(bytes,bytes+q->size);q->changed.notify_all();return pdPASS;
}
BaseType_t xQueueReset(QueueHandle_t q) {
    std::lock_guard<std::mutex> lock(q->mutex);q->items.clear();return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q,void* value,TickType_t wait) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if(wait==portMAX_DELAY)q->changed.wait(lock,[&]{return !q->items.empty();});
    else if(!q->changed.wait_for(lock,std::chrono::milliseconds(wait),[&]{return !q->items.empty();}))return pdFALSE;
    std::memcpy(value,q->items.front().data(),q->size);q->items.pop_front();return pdTRUE;
}
void vQueueDelete(QueueHandle_t q) { delete q;--queues; }
SemaphoreHandle_t xSemaphoreCreateBinary() { return xQueueCreate(1,1); }
BaseType_t xSemaphoreGive(SemaphoreHandle_t q) { const unsigned char c=1;return xQueueOverwrite(q,&c); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t q,TickType_t wait) { unsigned char c=0;return xQueueReceive(q,&c,wait); }
void vSemaphoreDelete(SemaphoreHandle_t q) { vQueueDelete(q); }
BaseType_t xTaskCreate(void(*run)(void*),const char*,unsigned,void* context,unsigned,TaskHandle_t* handle) {
    if(fail_task)return pdFALSE;
    *handle=context; workers.emplace_back(run,context);return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config,i2c_master_bus_handle_t* handle) {
    CHECK(!bus && config->sda_io_num==23 && config->scl_io_num==15);
    if(fail_bus)return ESP_ERR_NO_MEM;
    bus=true;*handle=&bus;return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t* config,i2c_master_dev_handle_t* handle) {
    CHECK(bus && !device && config->device_address==0x3c && config->scl_speed_hz==50000);
    if(fail_device)return ESP_ERR_NO_MEM;
    device=true;*handle=&device;return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const std::uint8_t* data,std::size_t size,int timeout) {
    CHECK(device && timeout==200 && std::this_thread::get_id()!=owner);
    in_transfer=true; entered=true;++calls;
    while(blocked)std::this_thread::sleep_for(1ms);
    if(size>2 && data[0]==0 && data[1]==0xae)++setups;
    if(size==2 && data[1]==0xaf)++on_commands;
    in_transfer=false;
    return offline ? ESP_FAIL : ESP_OK;
}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t) {
    CHECK(bus && !in_transfer && std::this_thread::get_id()!=owner);++resets;return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t) {
    CHECK(device && !in_transfer);device=false;return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t) {
    CHECK(bus && !device && !in_transfer);bus=false;return ESP_OK;
}
int main() {
    owner=std::this_thread::get_id();
    using bth::Status;
    bth::EspOledI2c adapter({0,23,15,50000,0x3c,true});
    CHECK(adapter.start_updates()==Status::invalid_state);
    for(int failure=0;failure<4;++failure) {
        fail_bus=failure==0;fail_device=failure==1;fail_queue=failure>=2?failure-2:-1;
        CHECK(adapter.init()==Status::no_memory);
        adapter.deinit();adapter.deinit();CHECK(!bus && !device && queues==0);
    }
    fail_bus=fail_device=false;fail_queue=-1;
    CHECK(adapter.init()==Status::ok);
    fail_task=true;CHECK(adapter.start_updates()==Status::no_memory);
    adapter.deinit();CHECK(!bus && !device && queues==0);fail_task=false;
    CHECK(adapter.init()==Status::ok && adapter.start_updates()==Status::ok);
    CHECK(adapter.start_updates()==Status::invalid_state);
    // Runtime can overwrite frames and poll while the bus transaction is blocked.
    blocked=true;entered=false;
    bth::OledUpdate update{bth::render_voltage(230,true),0,0x8f};
    CHECK(adapter.submit(update)==Status::ok);
    eventually([]{return entered.load();});
    update.generation=1;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<100;++i) { CHECK(adapter.submit(update)==Status::ok);CHECK(adapter.poll()==Status::ok); }
    CHECK(std::chrono::steady_clock::now()-start<200ms);
    blocked=false;
    eventually([]{return on_commands.load()==2;});
    CHECK(setups==2); // The overwritten new page retained its reinitialization.
    // An unplugged display is retried only on the next page.
    offline=true;update.generation=2;
    CHECK(adapter.submit(update)==Status::ok);
    eventually([&]{return adapter.poll()==Status::hardware_error;});
    const auto before=calls.load();
    CHECK(adapter.submit(update)==Status::ok);
    std::this_thread::sleep_for(20ms);CHECK(calls==before);
    offline=false;update.generation=3;
    CHECK(adapter.submit(update)==Status::ok);
    eventually([&]{return adapter.poll()==Status::ok;});
    CHECK(resets==1 && on_commands==3);
    // Shutdown joins an in-flight transaction before deleting driver resources.
    blocked=true;entered=false;update.generation=4;
    CHECK(adapter.submit(update)==Status::ok);eventually([]{return entered.load();});
    std::atomic<bool> stopped{false};
    std::thread stopper([&]{adapter.stop_updates();stopped=true;});
    std::this_thread::sleep_for(20ms);CHECK(!stopped && bus && device);
    blocked=false;stopper.join();join_workers();
    adapter.stop_updates();adapter.deinit();adapter.deinit();
    CHECK(!bus && !device && queues==0);
    // Boot with no responding display, then recover without restarting Runtime.
    offline=true;
    CHECK(adapter.init()==Status::ok && adapter.start_updates()==Status::ok);
    update.generation=0;CHECK(adapter.submit(update)==Status::ok);
    eventually([&]{return adapter.poll()==Status::hardware_error;});
    offline=false;update.generation=1;CHECK(adapter.submit(update)==Status::ok);
    eventually([&]{return adapter.poll()==Status::ok;});
    adapter.deinit();join_workers();CHECK(!bus && !device && queues==0);
    std::cout << "PASS: independent OLED task, bounded API, recovery, queue overwrite and joined cleanup\n";
}
