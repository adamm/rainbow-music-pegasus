// Tests for the fixed-point FFT that turns each frame of mic samples into the
// levels of its frequency bins, in mV.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fft.h"
#include "unity.h"

// The FFT sizes config_init() picks from the LED count and pattern.
static const uint16_t sizes[] = { 64, 128, 256, 512 };
#define N_SIZES (sizeof(sizes) / sizeof(sizes[0]))

// The ADC's range, in mV, which the mic's bias sits in the middle of.
#define RAIL_MV  3300
#define BIAS_MV  1650

static int samples[FFT_MAX_SAMPLES];
static float levels[FFT_MAX_SAMPLES];

void setUp(void)
{
}

void tearDown(void)
{
}


// The Blackman window, as fft.c has it.
static double blackman(int i, int n)
{
    double ratio = (double)i / (n - 1);
    return 0.42323 - 0.49755 * cos(2 * M_PI * ratio) + 0.07922 * cos(4 * M_PI * ratio);
}


// The level of `bin` in mV, the slow but plainly correct way: a direct DFT, in
// doubles, of the samples less their mean, through the window, scaled so a
// sine at the middle of a bin reads as its amplitude.
static double direct_level(const int* x, int n, int bin)
{
    double mean = 0;
    for (int i = 0; i < n; i++)
        mean += x[i];
    mean /= n;

    double re = 0, im = 0, window_sum = 0;
    for (int i = 0; i < n; i++) {
        double w = blackman(i, n);
        double angle = 2 * M_PI * bin * i / n;
        re += (x[i] - mean) * w * cos(angle);
        im -= (x[i] - mean) * w * sin(angle);
        window_sum += w;
    }
    return hypot(re, im) / (window_sum / 2);
}


// Run a frame of n samples through the FFT, for the levels of its lowest half.
static void transform(int n)
{
    fft_init(n);
    fft_load(samples);
    fft_compute();
    fft_magnitudes(levels, n / 2);
}


// The mic hears a tone of amplitude_mv in the middle of `bin`, in whole mV as
// mic.c reads it.
static void hear_tone(int n, int bin, double amplitude_mv)
{
    for (int i = 0; i < n; i++)
        samples[i] = BIAS_MV + (int)lround(amplitude_mv * sin(2 * M_PI * bin * i / n));
}


// The largest difference between fft.c's levels and direct_level()'s.
static double worst_error(int n)
{
    double worst = 0;
    for (int bin = 0; bin < n / 2; bin++) {
        double error = fabs(levels[bin] - direct_level(samples, n, bin));
        if (error > worst)
            worst = error;
    }
    return worst;
}


// Samples anywhere between the rails, as here, or a tone from rail to rail, as
// in the next test, are the most a frame can hold.  The sanitizers fail a test
// on any int32 overflow.
void test_levels_match_a_direct_dft_with_samples_between_the_rails(void)
{
    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        srand(n);
        for (int i = 0; i < n; i++)
            samples[i] = rand() % (RAIL_MV + 1);

        transform(n);

        double error = worst_error(n);
        char message[48];
        snprintf(message, sizeof(message), "%d-point FFT, worst error %.4f mV", n, error);
        TEST_ASSERT_TRUE_MESSAGE(error < 0.01, message);
    }
}


// Rounding the tone to whole mV, as mic.c reads it, moves its level by about
// 0.2 mV, whatever the FFT.
void test_a_tone_reads_as_its_amplitude_up_to_the_rails(void)
{
    static const double amplitudes_mv[] = { 100, 1000, BIAS_MV };

    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        for (int a = 0; a < 3; a++) {
            hear_tone(n, n / 8, amplitudes_mv[a]);

            transform(n);

            char message[48];
            snprintf(message, sizeof(message), "%d-point FFT, %.0f mV tone", n, amplitudes_mv[a]);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.5f, amplitudes_mv[a], levels[n / 8], message);
        }
    }
}


// Fixed point loses nothing that matters even at the quietest levels the
// LEDs care about, a fraction of FFT_NOISE_FLOOR_MV.
void test_quiet_tones_are_as_precise_as_loud_ones(void)
{
    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        hear_tone(n, n / 8, 3);

        transform(n);

        double error = worst_error(n);
        char message[48];
        snprintf(message, sizeof(message), "%d-point FFT, worst error %.6f mV", n, error);
        TEST_ASSERT_TRUE_MESSAGE(error < 0.001, message);
    }
}


// The mic idles at its bias, which mustn't light the lowest LEDs.
void test_the_dc_level_is_removed(void)
{
    static const int dc_mv[] = { BIAS_MV, 1651, RAIL_MV };

    for (int s = 0; s < N_SIZES; s++) {
        int n = sizes[s];
        for (int d = 0; d < 3; d++) {
            for (int i = 0; i < n; i++)
                samples[i] = dc_mv[d];

            transform(n);

            char message[48];
            snprintf(message, sizeof(message), "%d-point FFT at %d mV", n, dc_mv[d]);
            for (int bin = 0; bin < n / 2; bin++)
                TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 0, levels[bin], message);
        }
    }
}


// So main.c takes square roots of only the bins the LEDs show.
void test_levels_are_worked_out_for_only_the_lowest_bins_asked_for(void)
{
    hear_tone(64, 20, 100);
    for (int i = 0; i < 64; i++)
        levels[i] = -1;

    fft_init(64);
    fft_load(samples);
    fft_compute();
    fft_magnitudes(levels, 10);

    TEST_ASSERT_TRUE(levels[9] >= 0);
    TEST_ASSERT_EACH_EQUAL_FLOAT(-1, &levels[10], 54);
}


// The window and twiddle factors are worked out once per frame size, so the
// same frame must give the same levels every time.
void test_every_frame_gets_the_same_window(void)
{
    hear_tone(128, 9, 100);
    fft_init(128);
    float first[64];

    for (int frame = 0; frame < 3; frame++) {
        fft_load(samples);
        fft_compute();
        fft_magnitudes(levels, 64);
        if (frame == 0)
            memcpy(first, levels, sizeof(first));
        else
            TEST_ASSERT_EQUAL_FLOAT_ARRAY(first, levels, 64);
    }
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_levels_match_a_direct_dft_with_samples_between_the_rails);
    RUN_TEST(test_a_tone_reads_as_its_amplitude_up_to_the_rails);
    RUN_TEST(test_quiet_tones_are_as_precise_as_loud_ones);
    RUN_TEST(test_the_dc_level_is_removed);
    RUN_TEST(test_levels_are_worked_out_for_only_the_lowest_bins_asked_for);
    RUN_TEST(test_every_frame_gets_the_same_window);
    return UNITY_END();
}
