#pragma once

#include <stdint.h>

// Logs are dropped, but their arguments are still checked against the format.
__attribute__((format(printf, 2, 3)))
static inline void fake_esp_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

#define ESP_LOGE(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) fake_esp_log(tag, format, ##__VA_ARGS__)
