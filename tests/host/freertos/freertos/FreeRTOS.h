#pragma once
#include <cstddef>
#include <cstdint>
using TickType_t = std::uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned;
constexpr BaseType_t pdTRUE = 1, pdPASS = 1, pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
