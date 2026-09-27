/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_dsp.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"


#include "config.h"
#include "digipot.h"
#include "fft.h"
#include "leds.h"
#include "mic.h"

const static char *TAG = "main";



#define N_SAMPLES _config_total_samples

// Subtracted from every FFT bin so amp and ADC hiss doesn't flicker the LEDs.
#define FFT_NOISE_FLOOR 100

double sampling_frequency = 10000; // HZ
unsigned int sampling_period_us = 100; // (1000000 * (1.0 / sampling_frequency)
float sampling_time = 0.0128; // N_SAMPLES / sampling_frequency;


// Sample one frame from the mic, then let the mic adjust its sensitivity based
// on how loud the frame was.
static void read_frame(float* vReal, float* vImag)
{
    int voltage = 0;
    int min = INT_MAX;
    int max = INT_MIN;

    for (int i = 0; i < N_SAMPLES; i++) {
        voltage = mic_read();
        if (voltage < min)
            min = voltage;
        if (voltage > max)
            max = voltage;
        vReal[i] = (float)(voltage - 1650);
        vImag[i] = 0;
        esp_rom_delay_us(sampling_period_us);
    }

    mic_sensitivity_update(min, max);
}


void app_main(void)
{
    config_init();
    float* vReal = malloc(sizeof(float) * _config_total_samples);
    float* vImag = malloc(sizeof(float) * _config_total_samples);
    float* vDecay = malloc(sizeof(float) * _config_total_samples);
    uint8_t* colours = malloc(sizeof(uint8_t) * _config_total_samples);

    bzero(vReal, sizeof(float) * _config_total_samples);
    bzero(vImag, sizeof(float) * _config_total_samples);
    bzero(vDecay, sizeof(float) * _config_total_samples);
    bzero(colours, sizeof(uint8_t) * _config_total_samples);

    digipot_init();
    mic_init();
    leds_init();
    leds_scanning_start();
    fft_init(vReal, vImag, N_SAMPLES, sampling_frequency);

    uint64_t start_settle_time = esp_timer_get_time();

    // Give the mic a few seconds to settle its sensitivity to the room before
    // the light show starts.
    ESP_LOGI(TAG, "Settling mic sensitivity...");
    while (esp_timer_get_time()-start_settle_time < 3000000) {
        read_frame(vReal, vImag);
    }
    ESP_LOGI(TAG, "Finished settling mic sensitivity...");
    leds_scanning_stop();

    // Begin light show
    while (1) {
        read_frame(vReal, vImag);

        // ESP_LOGI(TAG, "raw");
        // dsps_view(vReal, N_SAMPLES, 64, 10, -100, 100, '-');
        fft_dcRemoval();
        fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude();


        for (int i = 0; i < N_SAMPLES; i++) {
            vReal[i] -= FFT_NOISE_FLOOR;
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

    mic_stop();
    digipot_stop();
}

