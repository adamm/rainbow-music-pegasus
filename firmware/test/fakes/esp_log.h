#pragma once

#include <stdint.h>

// Keeps only the last line logged, in fake.last_log, and checks the arguments
// against the format.
__attribute__((format(printf, 2, 3)))
void fake_esp_log(const char *tag, const char *format, ...);

#define ESP_LOGE(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
