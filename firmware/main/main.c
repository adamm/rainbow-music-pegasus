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
#include "config.h"
#include "fft.h"
#include "leds.h"

const static char *TAG = "rmp";



#define N_SAMPLES 256

__attribute__((aligned(16))) float vReal[N_SAMPLES];
__attribute__((aligned(16))) float vImag[N_SAMPLES];

double sampling_frequency = 10000; // HZ
unsigned int sampling_period_us = 100; // (1000000 * (1.0 / sampling_frequency)
float sampling_time = 0.0128; // N_SAMPLES / sampling_frequency;


void app_main(void)
{
    config_init();

    adc_init();
    leds_init();
    fft_init(vReal, vImag, N_SAMPLES, sampling_frequency);

    int voltage = 0;

    while (1) {
        for (int i = 0; i < N_SAMPLES; i++) {
            voltage = adc_read();
            vReal[i] = (float)(voltage - 1650);
            vImag[i] = 0;
            vTaskDelay(pdMS_TO_TICKS(sampling_period_us / 1000));
        }

        // ESP_LOGI(TAG, "raw");
        // dsps_view(vReal, N_SAMPLES, 64, 10, -100, 100, '-');
        fft_dcRemoval();
        fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude();

        uint8_t colours[N_SAMPLES] = {0};

        for (int i = 0; i < N_SAMPLES; i++) {
            if (vReal[i] < 200)
                vReal[i] = 0;
            else if (vReal[i] > 255)
                vReal[i] = 128;
            else
                vReal[i] /= 2;

            colours[i] = (uint8_t)vReal[i];
            //printf("%d ", colours[i]);
        }
        // printf("\n");
        // ESP_LOGI(TAG, "fft");
        // dsps_view(vReal, N_SAMPLES, 64, 10, 0, 255, '-');

        leds_display(colours, N_SAMPLES/2);
    }

    adc_stop();
}

