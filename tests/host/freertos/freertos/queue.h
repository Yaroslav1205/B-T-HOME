#pragma once
#include "FreeRTOS.h"
struct TestQueue;
using QueueHandle_t = TestQueue*;
QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t queue, void* item, TickType_t wait);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void* item);
void vQueueDelete(QueueHandle_t queue);
