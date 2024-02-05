#ifndef __ADC_H__
#define __ADC_H__

#include "esp_adc/adc_oneshot.h"

//ADC1 Channels
#define GPIO_ADC_CHANNEL         ADC_CHANNEL_2
#define GPIO_ADC_ATTEN           ADC_ATTEN_DB_12

bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
void adc_calibration_deinit(adc_cali_handle_t handle);

#endif