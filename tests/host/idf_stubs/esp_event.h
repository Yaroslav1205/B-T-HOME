#pragma once
#include <cstdint>
#include "esp_err.h"
using esp_event_base_t = const char*;
using esp_event_handler_instance_t = void*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, std::int32_t, void*);
inline constexpr const char* WIFI_EVENT = "wifi";
inline constexpr const char* IP_EVENT = "ip";
constexpr int ESP_EVENT_ANY_ID = -1, WIFI_EVENT_AP_START = 1, WIFI_EVENT_AP_STACONNECTED = 2,
    WIFI_EVENT_AP_STADISCONNECTED = 3, IP_EVENT_AP_STAIPASSIGNED = 4;
esp_err_t esp_event_loop_create_default();
esp_err_t esp_event_loop_delete_default();
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int, esp_event_handler_t, void*, esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int, esp_event_handler_instance_t);
