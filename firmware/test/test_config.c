// Tests for reading the LED-count jumpers.

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

void setUp(void)
{
    fake_idf_reset();
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


// The table in the README.
void test_jumpers_set_the_led_count_and_fft_size(void)
{
    static const struct {
        bool jp5, jp6, jp7;
        int leds;
        int samples;
    } boards[] = {
        // JP5   JP6    JP7   LEDs  FFT size
        { false, false, false, 10,  64 },
        { false, false, true,  12, 128 },
        { false, true,  false, 14, 128 },
        { false, true,  true,  16, 128 },
        { true,  false, false, 18, 256 },
        { true,  false, true,  20, 256 },
        { true,  true,  false, 22, 256 },
        { true,  true,  true,  24, 256 },
    };

    for (int i = 0; i < sizeof(boards) / sizeof(boards[0]); i++) {
        char closed[32];
        snprintf(closed, sizeof(closed), "JP5=%d JP6=%d JP7=%d closed", boards[i].jp5, boards[i].jp6, boards[i].jp7);

        fake_idf_reset();
        fake.gpio_level[JP5] = !boards[i].jp5;
        fake.gpio_level[JP6] = !boards[i].jp6;
        fake.gpio_level[JP7] = !boards[i].jp7;

        config_init();

        TEST_ASSERT_EQUAL_MESSAGE(boards[i].leds, _config_total_leds, closed);
        TEST_ASSERT_EQUAL_MESSAGE(boards[i].samples, _config_total_samples, closed);
    }
}


// Run config_init() on a fresh board with the LED-count jumpers set by the
// bits of `closed`: JP7 adds 2 LEDs, JP6 4 and JP5 8.
static void boot_with_jumpers(int closed)
{
    fake_idf_reset();
    fake.gpio_level[JP7] = !(closed & 1);
    fake.gpio_level[JP6] = !(closed & 2);
    fake.gpio_level[JP5] = !(closed & 4);

    config_init();
}


// So more LEDs split the same range more finely, rather than showing a
// different range.
void test_every_board_shows_0_hz_to_the_top_frequency(void)
{
    for (int closed = 0; closed < 8; closed++) {
        boot_with_jumpers(closed);

        // The LEDs show the lowest LEDs * 3 / 2 bins, each sample rate / FFT
        // size wide.
        float top_hz = (_config_total_leds * 3 / 2) * (float)_config_sample_freq_hz / _config_total_samples;

        char message[16];
        snprintf(message, sizeof(message), "%d LEDs", _config_total_leds);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1, CONFIG_LEDS_TOP_FREQ_HZ, top_hz, message);
    }
}


// Otherwise adc_continuous_config() refuses the rate, and the board reboots
// over and over.
void test_every_board_samples_within_the_adcs_range(void)
{
    for (int closed = 0; closed < 8; closed++) {
        boot_with_jumpers(closed);

        char message[16];
        snprintf(message, sizeof(message), "%d LEDs", _config_total_leds);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(SOC_ADC_SAMPLE_FREQ_THRES_HIGH, _config_sample_freq_hz, message);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(SOC_ADC_SAMPLE_FREQ_THRES_LOW, _config_sample_freq_hz, message);
    }
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_jumper_pins_are_inputs_with_pull_ups);
    RUN_TEST(test_jumpers_set_the_led_count_and_fft_size);
    RUN_TEST(test_every_board_shows_0_hz_to_the_top_frequency);
    RUN_TEST(test_every_board_samples_within_the_adcs_range);
    return UNITY_END();
}
