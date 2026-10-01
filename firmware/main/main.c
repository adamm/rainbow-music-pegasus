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


#include "config.h"
#include "digipot.h"
#include "fft.h"
#include "leds.h"
#include "mic.h"

const static char *TAG = "main";



#define N_SAMPLES _config_total_samples
// leds_display() only shows the lowest bins, three per pair of LEDs.
#define N_DISPLAYED_BINS (_config_total_leds * 3 / 2)

// Average weight of the Blackman window, which scales every FFT bin.
#define FFT_WINDOW_GAIN 0.42323f
// Each bin is converted to the amplitude, in mV at the ADC, of a sine wave that
// would produce it, so the LEDs behave the same whatever the frame size.  Bins
// under the noise floor stay dark so amp and ADC hiss doesn't flicker the LEDs,
// and bins at full scale light their LED at full brightness.  Full scale is well
// under the ADC's ~750 mV of headroom, leaving room for peaks across many bins.
#define FFT_NOISE_FLOOR_MV 8.5f
#define FFT_FULL_SCALE_MV  150.0f

// Once the sound in a bin drops, its LED fades to about a third (1/e) of its
// brightness in this time.  Fading by a fraction rather than a fixed step keeps
// dim LEDs from blinking out, so quiet music doesn't flicker.  Rises show
// immediately.
#define LED_DECAY_US 150000

double sampling_frequency = CONFIG_MIC_SAMPLE_FREQ_HZ;
float sampling_time = 0.0128; // N_SAMPLES / sampling_frequency;


// Sample one frame from the mic and replace vReal with its FFT bins in mV.
// Then let the mic adjust its sensitivity to fit the LEDs: too loud if the ADC
// clipped or the brightest LED would be at full brightness, too quiet if the
// loudest bin is under a quarter of that.  The 12 dB gap between the two is
// several sensitivity steps wide, so the gain settles instead of hunting.
static void read_spectrum(int* voltages, float* vReal, float* vImag)
{
    float peak = 0;
    bool clipped = mic_read_frame(voltages, N_SAMPLES);

    for (int i = 0; i < N_SAMPLES; i++) {
        vReal[i] = (float)(voltages[i] - 1650);
        vImag[i] = 0;
    }

    // ESP_LOGI(TAG, "raw");
    // dsps_view(vReal, N_SAMPLES, 64, 10, -100, 100, '-');
    fft_dcRemoval();
    fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);
    fft_compute(FFT_FORWARD);
    fft_complexToMagnitude();

    for (int i = 0; i < N_SAMPLES; i++) {
        vReal[i] /= N_SAMPLES / 2 * FFT_WINDOW_GAIN;
        if (i < N_DISPLAYED_BINS && vReal[i] > peak)
            peak = vReal[i];
    }

    mic_sensitivity_update(clipped || peak >= FFT_FULL_SCALE_MV, peak < FFT_FULL_SCALE_MV / 4);
}


// Replace each bin in vReal with its LED brightness, and set colours from it.
// vDecay holds each LED's brightness from the last frame, elapsed_us ago:
// louder bins show immediately, and quieter ones fade from there.
static void spectrum_to_colours(float* vReal, float* vDecay, uint8_t* colours, int64_t elapsed_us)
{
    float decay = expf(-(float)elapsed_us / LED_DECAY_US);

    for (int i = 0; i < N_SAMPLES; i++) {
        // Scale each bin to an LED brightness from 0 to 250.
        vReal[i] = (vReal[i] - FFT_NOISE_FLOOR_MV) * 250 / (FFT_FULL_SCALE_MV - FFT_NOISE_FLOOR_MV);
        if (vReal[i] < 0)
            vReal[i] = 0;
        else if (vReal[i] > 250)
            vReal[i] = 250;

        if (vReal[i] > vDecay[i])
            vDecay[i] = vReal[i];
        else
            vDecay[i] *= decay;

        colours[i] = (uint8_t)vDecay[i];
    }
}


void app_main(void)
{
    config_init();
    int* voltages = malloc(sizeof(int) * _config_total_samples);
    float* vReal = malloc(sizeof(float) * _config_total_samples);
    float* vImag = malloc(sizeof(float) * _config_total_samples);
    float* vDecay = malloc(sizeof(float) * _config_total_samples);
    uint8_t* colours = malloc(sizeof(uint8_t) * _config_total_samples);

    bzero(voltages, sizeof(int) * _config_total_samples);
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
        read_spectrum(voltages, vReal, vImag);
    }
    ESP_LOGI(TAG, "Finished settling mic sensitivity...");
    leds_scanning_stop();

    // Begin light show
    int64_t last_frame_time = esp_timer_get_time();
    while (1) {
        read_spectrum(voltages, vReal, vImag);

        // Base the fade on the time since the last frame, so its speed doesn't
        // depend on the frame size or on frames the driver dropped.
        int64_t now = esp_timer_get_time();
        spectrum_to_colours(vReal, vDecay, colours, now - last_frame_time);
        last_frame_time = now;

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

