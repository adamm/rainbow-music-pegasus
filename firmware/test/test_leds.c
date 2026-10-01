// Tests for sending FFT bins to the LEDs in the wings.
//
// This includes leds.c, rather than linking it, so each test can start with
// the LEDs dark.
#include "leds.c"

#include "fake_idf.h"
#include "unity.h"

void setUp(void)
{
    fake_idf_reset();
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
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


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_three_bins_light_a_right_and_left_pair_in_different_colours);
    RUN_TEST(test_only_the_fitted_leds_light);
    RUN_TEST(test_24_leds_show_the_first_36_bins_of_a_256_sample_fft);
    RUN_TEST(test_display_waits_for_the_strip_to_be_sent);
    return UNITY_END();
}
