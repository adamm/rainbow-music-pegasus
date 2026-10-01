#ifndef __MIC_H__
#define __MIC_H__

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_continuous.h"

bool mic_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void mic_calibration_deinit(adc_cali_handle_t handle);
void mic_init(void);
bool mic_read_frame(int* voltages, int total_samples);
void mic_sensitivity_update(bool loud, bool quiet);
void mic_stop(void);

#endif