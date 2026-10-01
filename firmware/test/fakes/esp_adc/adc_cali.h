#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/adc_types.h"

typedef struct adc_cali_scheme_t *adc_cali_handle_t;

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage);
