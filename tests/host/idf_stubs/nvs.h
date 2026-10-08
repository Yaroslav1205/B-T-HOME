#pragma once
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = std::uint32_t;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101;
constexpr int NVS_READWRITE = 1;
esp_err_t nvs_open_from_partition(const char*, const char*, int, nvs_handle_t*);
void nvs_close(nvs_handle_t);
const char* esp_err_to_name(esp_err_t);
