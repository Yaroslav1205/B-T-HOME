#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101;
const char* esp_err_to_name(esp_err_t);
