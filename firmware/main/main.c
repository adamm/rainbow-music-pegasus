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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"


#include "adc.h"
#include "config.h"
#include "fft.h"
#include "leds.h"

const static char *TAG = "main";



#define N_SAMPLES _config_total_samples

double sampling_frequency = 10000; // HZ
unsigned int sampling_period_us = 100; // (1000000 * (1.0 / sampling_frequency)
float sampling_time = 0.0128; // N_SAMPLES / sampling_frequency;


void app_main(void)
{
    config_init();
    float* vReal = malloc(sizeof(float) * _config_total_samples);
    float* vImag = malloc(sizeof(float) * _config_total_samples);
    float* vCalib = malloc(sizeof(float) * _config_total_samples);
    float* vDecay = malloc(sizeof(float) * _config_total_samples);
    uint8_t* colours = malloc(sizeof(uint8_t) * _config_total_samples);

    bzero(vReal, sizeof(float) * _config_total_samples);
    bzero(vImag, sizeof(float) * _config_total_samples);
    bzero(vCalib, sizeof(float) * _config_total_samples);
    bzero(vDecay, sizeof(float) * _config_total_samples);
    bzero(colours, sizeof(uint8_t) * _config_total_samples);

    adc_init();
    leds_init();
    leds_scanning_start();
    fft_init(vReal, vImag, N_SAMPLES, sampling_frequency);

    int voltage = 0;
    uint64_t start_calib_time = esp_timer_get_time();

    // Collect a few seconds of ambient noise.  This will be subtracted from the
    // light show value, allowing the device to work in noisier environments.
    // Obivously don't play any music while calibrating!
    ESP_LOGI(TAG, "Starting calibration...");
    while (esp_timer_get_time()-start_calib_time < 3000000) {
        for (int i = 0; i < N_SAMPLES; i++) {
            voltage = adc_read();
            vReal[i] = (float)(voltage - 1650);
            vImag[i] = 0;
            vTaskDelay(pdMS_TO_TICKS(sampling_period_us / 1000));
        }

        fft_dcRemoval();
        fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude();

        for (int i = 0; i < N_SAMPLES; i++) {
            if (vReal[i] > vCalib[i])
                vCalib[i] = vReal[i] + 100;
        }
    }
    ESP_LOGI(TAG, "Finished calibration...");
    dsps_view(vCalib, N_SAMPLES, 64, 10, 0, 1000, '-');
    leds_scanning_stop();

    // Begin light show
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


        for (int i = 0; i < N_SAMPLES; i++) {
            vReal[i] -= vCalib[i];
            if (vReal[i] < 0)
                vReal[i] = 0;
            else if (vReal[i] > 2000)
                vReal[i] = 2000;
            vReal[i] /= 8;

            if (vReal[i] > vDecay[i])
                vDecay[i] = vReal[i];
            else
                vDecay[i] -= 10;
            if (vDecay[i] < 0)
                vDecay[i] = 0;

            colours[i] = (uint8_t)vDecay[i];
        }
        // printf("R: ");
        // for (int i = 0; i < 16; i++) {
        //     printf("%0.1f ", vReal[i]);
        // }
        // printf("\nD: ");
        // for (int i = 0; i < 16; i++) {
        //     printf("%0.1f ", vDecay[i]);
        // }
        // printf("\n");
        // ESP_LOGI(TAG, "fft");
        // dsps_view(vReal, N_SAMPLES, 64, 10, 0, 255, '-');

        leds_display(colours, N_SAMPLES/2);
    }

    adc_stop();
}

