#ifndef __MIC_H__
#define __MIC_H__

#include "esp_adc/adc_oneshot.h"

bool mic_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void mic_calibration_deinit(adc_cali_handle_t handle);
void mic_init(void);
int mic_read(void);
void mic_stop(void);

#endif