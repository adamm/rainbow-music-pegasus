/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_dsp.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "adc.h"
#include "fft.h"

const static char *TAG = "main";



#define N_SAMPLES 128

__attribute__((aligned(16))) float fft_input[N_SAMPLES];
__attribute__((aligned(16))) float fft_output[N_SAMPLES];

__attribute__((aligned(16))) float diff_y[N_SAMPLES/2];
__attribute__((aligned(16))) float sum_y[N_SAMPLES/2];

double sampling_frequency = 10000; // HZ
unsigned int sampling_period_us = 100; // (1000000 * (1.0 / sampling_frequency)
float sampling_time = 0.0128; // N_SAMPLES / sampling_frequency;


void app_main(void)
{
    // TODO: Move adc initialization and configuration functions to adc.h
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc_handle));

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = GPIO_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, GPIO_ADC_CHANNEL, &adc_channel_config));

    adc_cali_handle_t adc_cali_channel_handle = NULL;
    bool calibrated = adc_calibration_init(ADC_UNIT_1, GPIO_ADC_CHANNEL, GPIO_ADC_ATTEN, &adc_cali_channel_handle);

    int adc_raw;
    int voltage;

    fft_config_t* fft_plan = fft_init(N_SAMPLES, FFT_REAL, FFT_FORWARD, fft_input, fft_output);

    while (1) {
        for (int i = 0; i < N_SAMPLES; i++) {
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, GPIO_ADC_CHANNEL, &adc_raw));
            //ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, GPIO_ADC_CHANNEL, adc_raw[0][0]);
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_channel_handle, adc_raw, &voltage));
            fft_plan->input[i] = (float)(voltage - 1650);
            //ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d V", ADC_UNIT_1 + 1, GPIO_ADC_CHANNEL, voltage);
            vTaskDelay(pdMS_TO_TICKS(sampling_period_us / 1000));
        }
        fft_execute(fft_plan);

        dsps_view(fft_plan->output, N_SAMPLES, 128, 10, 300, 4000, '|');
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    if (calibrated) {
        adc_calibration_deinit(adc_cali_channel_handle);
    }

}

