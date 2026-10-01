#pragma once

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL                -1
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107

void fake_esp_error_check_failed(esp_err_t rc, const char *file, int line, const char *function, const char *expression);

// Unlike the real one, a failed check fails the current test rather than
// aborting, so the remaining tests still run.
#define ESP_ERROR_CHECK(x) do {                                                     \
        esp_err_t err_rc_ = (x);                                                    \
        if (err_rc_ != ESP_OK)                                                      \
            fake_esp_error_check_failed(err_rc_, __FILE__, __LINE__, __func__, #x); \
    } while (0)
