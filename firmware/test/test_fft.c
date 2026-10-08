// Tests for the FFT that turns each frame of mic samples into frequency bins.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fft.h"
#include "unity.h"

#define SAMPLE_FREQ_HZ  10000
#define MAX_SAMPLES     512

// The FFT sizes config_init() picks from the LED count and pattern.
static const uint16_t sizes[] = { 64, 128, 256, 512 };
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
        fft_complexToMagnitude(n);

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
        // Bins are SAMPLE_FREQ_HZ / n apart, so 1250 Hz is bin 8, 16, 32 or 64.
        int bin = 1250 * n / SAMPLE_FREQ_HZ;

        for (int i = 0; i < n; i++) {
            vReal[i] = (float)(100 * sin(2 * M_PI * 1250 * i / SAMPLE_FREQ_HZ));
            vImag[i] = 0;
        }

        fft_init(vReal, vImag, n, SAMPLE_FREQ_HZ);
        fft_compute(FFT_FORWARD);
        fft_complexToMagnitude(n);

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


// So main.c can take square roots of only the bins the LEDs show.
void test_magnitudes_are_computed_for_only_the_lowest_bins_asked_for(void)
{
    for (int i = 0; i < 64; i++) {
        vReal[i] = (float)(100 * sin(2 * M_PI * 3 * i / 64));
        vImag[i] = 0;
    }

    fft_init(vReal, vImag, 64, SAMPLE_FREQ_HZ);
    fft_compute(FFT_FORWARD);
    // Bin 61 mirrors the tone: 3200 in magnitude, but all imaginary.
    float untouched = vReal[61];
    fft_complexToMagnitude(10);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100 * 64 / 2, vReal[3]);
    TEST_ASSERT_EQUAL_FLOAT(untouched, vReal[61]);
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


// The window's weights are worked out once and reused, so every frame must get
// the same ones.
void test_the_window_is_the_same_on_every_frame(void)
{
    float first[MAX_SAMPLES];
    fft_init(vReal, vImag, 128, SAMPLE_FREQ_HZ);

    for (int frame = 0; frame < 3; frame++) {
        for (int i = 0; i < 128; i++)
            vReal[i] = 1;
        fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);

        if (frame == 0)
            memcpy(first, vReal, 128 * sizeof(float));
        else
            TEST_ASSERT_EQUAL_FLOAT_ARRAY(first, vReal, 128);
    }
}


void test_a_different_window_gets_its_own_weights(void)
{
    fft_init(vReal, vImag, 64, SAMPLE_FREQ_HZ);
    for (int i = 0; i < 64; i++)
        vReal[i] = 1;
    fft_windowing(FFT_WIN_TYP_BLACKMAN, FFT_FORWARD);

    for (int i = 0; i < 64; i++)
        vReal[i] = 1;
    fft_windowing(FFT_WIN_TYP_RECTANGLE, FFT_FORWARD);

    TEST_ASSERT_EACH_EQUAL_FLOAT(1, vReal, 64);
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fft_matches_a_direct_dft);
    RUN_TEST(test_a_tone_peaks_in_the_bin_for_its_frequency);
    RUN_TEST(test_magnitudes_are_computed_for_only_the_lowest_bins_asked_for);
    RUN_TEST(test_dc_removal_subtracts_the_mean);
    RUN_TEST(test_blackman_window_tapers_both_ends_alike);
    RUN_TEST(test_the_window_is_the_same_on_every_frame);
    RUN_TEST(test_a_different_window_gets_its_own_weights);
    return UNITY_END();
}
