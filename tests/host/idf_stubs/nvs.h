#pragma once
#include <cstdint>
#include "esp_err.h"
using nvs_handle_t = std::uint32_t;
constexpr int NVS_READWRITE = 1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
esp_err_t nvs_get_u32(nvs_handle_t, const char*, std::uint32_t*);
esp_err_t nvs_set_u32(nvs_handle_t, const char*, std::uint32_t);
esp_err_t nvs_commit(nvs_handle_t);
esp_err_t nvs_open_from_partition(const char*, const char*, int, nvs_handle_t*);
void nvs_close(nvs_handle_t);

#include <cstddef>
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, std::size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, std::size_t);
esp_err_t nvs_get_u8(nvs_handle_t, const char*, std::uint8_t*);
esp_err_t nvs_set_u8(nvs_handle_t, const char*, std::uint8_t);
