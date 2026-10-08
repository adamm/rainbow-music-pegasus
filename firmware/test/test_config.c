// Tests for reading the LED-count jumpers, and sizing the FFT for them and the
// LED pattern.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "fake_idf.h"
#include "soc/soc_caps.h"
#include "unity.h"

#define JP5 CONFIG_GPIO_TOTAL_LEDS_ADD_8
#define JP6 CONFIG_GPIO_TOTAL_LEDS_ADD_4
#define JP7 CONFIG_GPIO_TOTAL_LEDS_ADD_2

static const config_pattern_t patterns[] = { CONFIG_PATTERN_A, CONFIG_PATTERN_B, CONFIG_PATTERN_C };
#define N_PATTERNS (sizeof(patterns) / sizeof(patterns[0]))

void setUp(void)
{
    fake_idf_reset();
    _config_pattern = CONFIG_LEDS_PATTERN;
}

void tearDown(void)
{
}


void test_jumper_pins_are_inputs_with_pull_ups(void)
{
    config_init();

    // A closed jumper pulls its pin low, so an open one must read high.
    TEST_ASSERT_EQUAL_HEX32((1 << 5) | (1 << 6) | (1 << 7), (uint32_t)fake.gpio_config.pin_bit_mask);
    TEST_ASSERT_EQUAL(GPIO_MODE_INPUT, fake.gpio_config.mode);
    TEST_ASSERT_EQUAL(GPIO_PULLUP_ENABLE, fake.gpio_config.pull_up_en);
    TEST_ASSERT_EQUAL(GPIO_PULLDOWN_DISABLE, fake.gpio_config.pull_down_en);
}


// The table in the README.  Patterns B and C show twice as many bins as A, so
// they take FFT frames twice as large.
void test_jumpers_and_the_pattern_set_the_led_count_and_fft_size(void)
{
    static const struct {
        bool jp5, jp6, jp7;
        int leds;
        int samples_a;
        int samples_b_c;
    } boards[] = {
        // JP5   JP6    JP7   LEDs  FFT size in A, and in B and C
        { false, false, false, 10,  64, 128 },
        { false, false, true,  12, 128, 256 },
        { false, true,  false, 14, 128, 256 },
        { false, true,  true,  16, 128, 256 },
        { true,  false, false, 18, 256, 512 },
        { true,  false, true,  20, 256, 512 },
        { true,  true,  false, 22, 256, 512 },
        { true,  true,  true,  24, 256, 512 },
    };

    for (int p = 0; p < N_PATTERNS; p++) {
        for (int i = 0; i < sizeof(boards) / sizeof(boards[0]); i++) {
            char closed[48];
            snprintf(closed, sizeof(closed), "pattern %c, JP5=%d JP6=%d JP7=%d closed",
                     'A' + patterns[p], boards[i].jp5, boards[i].jp6, boards[i].jp7);

            fake_idf_reset();
            fake.gpio_level[JP5] = !boards[i].jp5;
            fake.gpio_level[JP6] = !boards[i].jp6;
            fake.gpio_level[JP7] = !boards[i].jp7;
            _config_pattern = patterns[p];

            config_init();

            int samples = patterns[p] == CONFIG_PATTERN_A ? boards[i].samples_a : boards[i].samples_b_c;
            TEST_ASSERT_EQUAL_MESSAGE(boards[i].leds, _config_total_leds, closed);
            TEST_ASSERT_EQUAL_MESSAGE(samples, _config_total_samples, closed);
        }
    }
}


void test_pattern_a_shows_three_bins_per_pair_and_b_and_c_three_per_led(void)
{
    _config_total_leds = 10;
    _config_pattern = CONFIG_PATTERN_A;
    TEST_ASSERT_EQUAL(15, config_displayed_bins());
    _config_pattern = CONFIG_PATTERN_B;
    TEST_ASSERT_EQUAL(30, config_displayed_bins());
    _config_pattern = CONFIG_PATTERN_C;
    TEST_ASSERT_EQUAL(30, config_displayed_bins());

    _config_total_leds = 24;
    _config_pattern = CONFIG_PATTERN_A;
    TEST_ASSERT_EQUAL(36, config_displayed_bins());
    _config_pattern = CONFIG_PATTERN_B;
    TEST_ASSERT_EQUAL(72, config_displayed_bins());
    _config_pattern = CONFIG_PATTERN_C;
    TEST_ASSERT_EQUAL(72, config_displayed_bins());
}


// Run config_init() on a fresh board showing `pattern`, with the LED-count
// jumpers set by the bits of `closed`: JP7 adds 2 LEDs, JP6 4 and JP5 8.
static void boot_with_jumpers(config_pattern_t pattern, int closed)
{
    fake_idf_reset();
    fake.gpio_level[JP7] = !(closed & 1);
    fake.gpio_level[JP6] = !(closed & 2);
    fake.gpio_level[JP5] = !(closed & 4);
    _config_pattern = pattern;

    config_init();
}


// So more LEDs split the same range more finely, rather than showing a
// different range, and every pattern shows the same range too.
void test_every_board_shows_0_hz_to_the_top_frequency_in_every_pattern(void)
{
    for (int p = 0; p < N_PATTERNS; p++) {
        for (int closed = 0; closed < 8; closed++) {
            boot_with_jumpers(patterns[p], closed);

            // The LEDs show the lowest bins, each sample rate / FFT size wide.
            float top_hz = config_displayed_bins() * (float)_config_sample_freq_hz / _config_total_samples;

            char message[32];
            snprintf(message, sizeof(message), "pattern %c, %d LEDs", 'A' + patterns[p], _config_total_leds);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1, CONFIG_LEDS_TOP_FREQ_HZ, top_hz, message);
        }
    }
}


// So sound above half the sample rate folds back onto the LEDs no more in
// patterns B and C than in A, and the mic's anti-aliasing filters cut it as
// much.
void test_every_pattern_samples_a_board_at_the_same_rate(void)
{
    for (int closed = 0; closed < 8; closed++) {
        boot_with_jumpers(CONFIG_PATTERN_A, closed);
        uint32_t rate_a = _config_sample_freq_hz;

        for (int p = 0; p < N_PATTERNS; p++) {
            boot_with_jumpers(patterns[p], closed);

            char message[32];
            snprintf(message, sizeof(message), "pattern %c, %d LEDs", 'A' + patterns[p], _config_total_leds);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(rate_a, _config_sample_freq_hz, message);
        }
    }
}


// Otherwise adc_continuous_config() refuses the rate, and the board reboots
// over and over.
void test_every_board_samples_within_the_adcs_range(void)
{
    for (int p = 0; p < N_PATTERNS; p++) {
        for (int closed = 0; closed < 8; closed++) {
            boot_with_jumpers(patterns[p], closed);

            char message[32];
            snprintf(message, sizeof(message), "pattern %c, %d LEDs", 'A' + patterns[p], _config_total_leds);
            TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(SOC_ADC_SAMPLE_FREQ_THRES_HIGH, _config_sample_freq_hz, message);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(SOC_ADC_SAMPLE_FREQ_THRES_LOW, _config_sample_freq_hz, message);
        }
    }
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_jumper_pins_are_inputs_with_pull_ups);
    RUN_TEST(test_jumpers_and_the_pattern_set_the_led_count_and_fft_size);
    RUN_TEST(test_pattern_a_shows_three_bins_per_pair_and_b_and_c_three_per_led);
    RUN_TEST(test_every_board_shows_0_hz_to_the_top_frequency_in_every_pattern);
    RUN_TEST(test_every_pattern_samples_a_board_at_the_same_rate);
    RUN_TEST(test_every_board_samples_within_the_adcs_range);
    return UNITY_END();
}
