// Tests for the FFT that turns each frame of mic samples into frequency bins.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fft.h"
#include "unity.h"

#define SAMPLE_FREQ_HZ  10000
#define MAX_SAMPLES     256

// The FFT sizes config_init() picks from the LED count.
static const uint16_t sizes[] = { 64, 128, 256 };
#define N_SIZES (sizeof(sizes) / sizeof(sizes[0]))

static float vReal[MAX_SAMPLES];
static float vImag[MAX_SAMPLES];

void setUp(void)
{
}

void tearDown(void)
{
}


// A direct DFT: slow, but plainly correct.
static double dft_magnitude(const float *x, int n, int bin)
{
    double re = 0;
    double im = 0;
    for (int i = 0; i < n; i++) {
        double angle = 2 * M_PI * bin * i / n;
        re += x[i] * cos(angle);
        im -= x[i] * sin(angle);
    }
    return hypot(re, im);
}


void test_fft_matches_a_direct_dft(void)
{
    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        float input[MAX_SAMPLES];

        srand(n);
        for (int i = 0; i < n; i++) {
            input[i] = (float)(rand() % 2001 - 1000);
            vReal[i] = input[i];
            vImag[i] = 0;
        }

        fft_init(vReal, vImag, n, SAMPLE_FREQ_HZ);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude();

        // Rounding errors grow with the FFT size, but stay far smaller than
        // any real mistake would make.
        float tolerance = n * 0.01f;
        for (int bin = 0; bin < n; bin++) {
            char message[32];
            snprintf(message, sizeof(message), "%d-point FFT, bin %d", n, bin);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance, dft_magnitude(input, n, bin), vReal[bin], message);
        }
    }
}


void test_a_tone_peaks_in_the_bin_for_its_frequency(void)
{
    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        // Bins are SAMPLE_FREQ_HZ / n apart, so 1250 Hz is bin 8, 16 or 32.
        int bin = 1250 * n / SAMPLE_FREQ_HZ;

        for (int i = 0; i < n; i++) {
            vReal[i] = (float)(100 * sin(2 * M_PI * 1250 * i / SAMPLE_FREQ_HZ));
            vImag[i] = 0;
        }

        fft_init(vReal, vImag, n, SAMPLE_FREQ_HZ);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude();

        int loudest = 0;
        for (int i = 1; i < n / 2; i++) {
            if (vReal[i] > vReal[loudest])
                loudest = i;
        }
        char message[32];
        snprintf(message, sizeof(message), "%d-point FFT", n);
        TEST_ASSERT_EQUAL_MESSAGE(bin, loudest, message);
        // Without a window, a tone of amplitude A gives A * n / 2.
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 100 * n / 2, vReal[bin], message);
    }
}


void test_dc_removal_subtracts_the_mean(void)
{
    // The mic idles at the middle of the ADC's range.
    const float samples[8] = { 1650, 1700, 1600, 1650, 1800, 1500, 1650, 1650 };
    for (int i = 0; i < 8; i++)
        vReal[i] = samples[i];

    fft_init(vReal, vImag, 8, SAMPLE_FREQ_HZ);
    fft_dcRemoval();

    const float expected[8] = { 0, 50, -50, 0, 150, -150, 0, 0 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, vReal, 8);
}


void test_blackman_window_tapers_both_ends_alike(void)
{
    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        for (int i = 0; i < n; i++)
            vReal[i] = 1;

        fft_init(vReal, vImag, n, SAMPLE_FREQ_HZ);
        fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);

        TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, vReal[0]);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 1, vReal[n / 2]);
        for (int i = 0; i < n / 2; i++)
            TEST_ASSERT_EQUAL_FLOAT(vReal[i], vReal[n - 1 - i]);
    }
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fft_matches_a_direct_dft);
    RUN_TEST(test_a_tone_peaks_in_the_bin_for_its_frequency);
    RUN_TEST(test_dc_removal_subtracts_the_mean);
    RUN_TEST(test_blackman_window_tapers_both_ends_alike);
    return UNITY_END();
}
