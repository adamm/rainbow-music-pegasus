#ifndef __ADC_H__
#define __ADC_H__

#include "esp_adc/adc_oneshot.h"

bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void adc_calibration_deinit(adc_cali_handle_t handle);
void adc_init(void);
int adc_read(void);
void adc_stop(void);

#endif