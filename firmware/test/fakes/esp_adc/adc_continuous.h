#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/adc_types.h"

typedef struct adc_continuous_ctx_t *adc_continuous_handle_t;

typedef struct {
    uint32_t max_store_buf_size;
    uint32_t conv_frame_size;
    struct {
        uint32_t flush_pool: 1;
    } flags;
} adc_continuous_handle_cfg_t;

typedef struct {
    uint32_t pattern_num;
    adc_digi_pattern_config_t *adc_pattern;
    uint32_t sample_freq_hz;
    adc_digi_convert_mode_t conv_mode;
    adc_digi_output_format_t format;
} adc_continuous_config_t;

esp_err_t adc_continuous_new_handle(const adc_continuous_handle_cfg_t *hdl_config, adc_continuous_handle_t *ret_handle);
esp_err_t adc_continuous_config(adc_continuous_handle_t handle, const adc_continuous_config_t *config);
esp_err_t adc_continuous_start(adc_continuous_handle_t handle);
esp_err_t adc_continuous_read(adc_continuous_handle_t handle, uint8_t *buf, uint32_t length_max, uint32_t *out_length, uint32_t timeout_ms);
esp_err_t adc_continuous_stop(adc_continuous_handle_t handle);
esp_err_t adc_continuous_deinit(adc_continuous_handle_t handle);
