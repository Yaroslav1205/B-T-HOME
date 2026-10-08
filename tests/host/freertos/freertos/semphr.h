#pragma once
#include "queue.h"
using SemaphoreHandle_t = QueueHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
