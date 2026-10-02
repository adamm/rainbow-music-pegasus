// Tests for sending FFT bins to the LEDs in the wings.
//
// This includes leds.c, rather than linking it, so each test can start with
// the LEDs dark and the battery charged.
#include "leds.c"

#include <stdio.h>

#include "fake_idf.h"
#include "unity.h"

void setUp(void)
{
    fake_idf_reset();
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    low_battery = false;
    leds_init();
}

void tearDown(void)
{
}


// The bytes sent to the LED at `index` along the strip.  WS2812Bs take them
// as green, red, blue.
static const uint8_t *led(int index)
{
    return &fake.rmt_sent[index * 3];
}


void test_each_three_bins_light_a_right_and_left_pair_in_different_colours(void)
{
    _config_total_leds = 10;
    uint8_t bins[32];
    for (int i = 0; i < 32; i++)
        bins[i] = 100 + i;

    leds_display(bins, 32);

    for (int pair = 0; pair < 5; pair++) {
        const uint8_t *b = &bins[pair * 3];
        // So a tone in the pair's first bin is green on the right wing and
        // blue on the left.
        const uint8_t right[3] = { b[0], b[1], b[2] };
        const uint8_t left[3] = { b[1], b[2], b[0] };
        TEST_ASSERT_EQUAL_UINT8_ARRAY(right, led(pair * 2), 3);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(left, led(pair * 2 + 1), 3);
    }
}


void test_only_the_fitted_leds_light(void)
{
    _config_total_leds = 10;
    uint8_t bins[32];
    memset(bins, 250, sizeof(bins));

    leds_display(bins, 32);

    TEST_ASSERT_EQUAL(CONFIG_MAX_LEDS * 3, fake.rmt_sent_bytes);
    TEST_ASSERT_EACH_EQUAL_UINT8(250, led(0), 10 * 3);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, led(10), (CONFIG_MAX_LEDS - 10) * 3);
}


void test_24_leds_show_the_first_36_bins_of_a_256_sample_fft(void)
{
    _config_total_leds = 24;
    uint8_t bins[128];
    for (int i = 0; i < 128; i++)
        bins[i] = i;

    // main.c passes half the FFT, far more bins than there are LEDs.
    leds_display(bins, 128);

    const uint8_t last_right[3] = { 33, 34, 35 };
    const uint8_t last_left[3] = { 34, 35, 33 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(last_right, led(22), 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(last_left, led(23), 3);
}


void test_display_waits_for_the_strip_to_be_sent(void)
{
    _config_total_leds = 10;
    uint8_t bins[32] = { 0 };

    leds_display(bins, 32);

    // Otherwise the next frame could change the pixels while they're sent.
    TEST_ASSERT_EQUAL(1, fake.rmt_transmits);
    TEST_ASSERT_FALSE(fake.rmt_busy);
}


static const uint8_t blink_red[3] = { 0, LEDS_LOW_BATTERY_RED, 0 };  // green, red, blue

// Show bins all at `brightness`, t_us into a blink period.
static void display_at(int64_t t_us, uint8_t brightness)
{
    uint8_t bins[128];
    memset(bins, brightness, sizeof(bins));

    fake.now_us = 10 * LEDS_LOW_BATTERY_PERIOD_US + t_us;
    leds_display(bins, 128);
}


void test_a_low_battery_blinks_the_first_led_in_each_wing_red(void)
{
    static const int fitted[] = { 10, 16, 24 };
    leds_show_low_battery(true);

    for (int i = 0; i < sizeof(fitted) / sizeof(fitted[0]); i++) {
        int leds = fitted[i];
        char message[16];
        snprintf(message, sizeof(message), "%d LEDs", leds);
        _config_total_leds = leds;
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));

        display_at(0, 200);

        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(blink_red, led(0), 3, message);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(blink_red, led(1), 3, message);
        // The rest of the wings still show the music.
        TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(200, led(2), (leds - 2) * 3, message);
        if (leds < CONFIG_MAX_LEDS)
            TEST_ASSERT_EACH_EQUAL_UINT8_MESSAGE(0, led(leds), (CONFIG_MAX_LEDS - leds) * 3, message);
    }
}


void test_the_blink_is_brief_and_the_music_shows_in_between(void)
{
    _config_total_leds = 10;
    leds_show_low_battery(true);

    display_at(LEDS_LOW_BATTERY_ON_US - 1, 200);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(blink_red, led(1), 3);

    display_at(LEDS_LOW_BATTERY_ON_US, 200);
    TEST_ASSERT_EACH_EQUAL_UINT8(200, led(0), 2 * 3);

    display_at(LEDS_LOW_BATTERY_PERIOD_US - 1, 200);
    TEST_ASSERT_EACH_EQUAL_UINT8(200, led(0), 2 * 3);
}


// So the blink can't be mistaken for the music.
void test_the_music_never_lights_both_leds_of_a_pair_pure_red(void)
{
    _config_total_leds = 10;
    uint8_t bins[32] = { 0 };

    // Every mix of the pair's three bins being dark or lit.
    for (int lit = 0; lit < 8; lit++) {
        for (int b = 0; b < 3; b++)
            bins[b] = (lit & (1 << b)) ? 250 : 0;
        leds_display(bins, 32);

        bool right_red = led(0)[1] && !led(0)[0] && !led(0)[2];
        bool left_red = led(1)[1] && !led(1)[0] && !led(1)[2];
        TEST_ASSERT_FALSE(right_red && left_red);
    }
}


void test_a_charged_battery_never_blinks(void)
{
    _config_total_leds = 10;
    leds_show_low_battery(false);

    for (int64_t t = 0; t < LEDS_LOW_BATTERY_PERIOD_US; t += LEDS_LOW_BATTERY_ON_US / 2) {
        display_at(t, 200);
        TEST_ASSERT_EACH_EQUAL_UINT8(200, led(0), 10 * 3);
    }
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_three_bins_light_a_right_and_left_pair_in_different_colours);
    RUN_TEST(test_only_the_fitted_leds_light);
    RUN_TEST(test_24_leds_show_the_first_36_bins_of_a_256_sample_fft);
    RUN_TEST(test_display_waits_for_the_strip_to_be_sent);
    RUN_TEST(test_a_low_battery_blinks_the_first_led_in_each_wing_red);
    RUN_TEST(test_the_blink_is_brief_and_the_music_shows_in_between);
    RUN_TEST(test_the_music_never_lights_both_leds_of_a_pair_pure_red);
    RUN_TEST(test_a_charged_battery_never_blinks);
    return UNITY_END();
}
