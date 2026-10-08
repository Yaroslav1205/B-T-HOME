#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
BaseType_t xTaskCreate(void (*worker)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*);
TickType_t xTaskGetTickCount();
void vTaskDelete(TaskHandle_t);
