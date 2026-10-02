// Tests for measuring the battery and deciding when it's low.
//
// This includes battery.c, rather than linking it, so each test can start from
// a freshly booted board.
#include "battery.c"

#include "fake_idf.h"
#include "unity.h"

#define BATTERY     CONFIG_BATTERY_CHANNEL

// How often app_main() checks the battery during the light show.
#define CHECK_US    10000000

static int64_t start_us;

// Put battery.c back how it is at boot, then start it up as app_main() does.
static void boot(void)
{
    battery_handle = NULL;
    battery_cali_handle = NULL;
    battery_mv = 0;
    battery_read_time = 0;
    battery_low = false;

    battery_init();
}

void setUp(void)
{
    fake_idf_reset();
    start_us = fake.now_us;
    boot();
}

void tearDown(void)
{
}


// Check a battery at mv, t_us into the test, and return whether it's low.
// R12 and R13 halve it at GPIO 0.
static bool check_at(int64_t t_us, int mv)
{
    fake.now_us = start_us + t_us;
    fake.adc_oneshot_raw[BATTERY] = mv / 2;
    return battery_check();
}


void test_init_reads_gpio_0_on_adc1(void)
{
    TEST_ASSERT_EQUAL(ADC_UNIT_1, fake.adc_oneshot_unit.unit_id);
    TEST_ASSERT_EQUAL(ADC_CHANNEL_0, fake.adc_oneshot_channel);  // GPIO 0

    // So a full battery, 2.1 V at GPIO 0, or v1.2's USB supply, about 2.3 V,
    // is in range.
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, fake.adc_oneshot_channel_config.atten);
}


void test_a_reading_is_twice_the_voltage_at_gpio_0(void)
{
    fake.adc_oneshot_raw[BATTERY] = 1850;

    battery_check();

    TEST_ASSERT_EQUAL_FLOAT(3700, battery_mv);
}


void test_a_reading_without_calibration_assumes_3100mv_full_scale(void)
{
    fake.adc_cali_in_efuse = false;
    boot();

    fake.adc_oneshot_raw[BATTERY] = 2048;
    battery_check();

    TEST_ASSERT_EQUAL_FLOAT(2 * 1550, battery_mv);
}


void test_a_full_battery_is_not_low(void)
{
    TEST_ASSERT_FALSE(check_at(0, 4200));
}


// The first reading is at power-on, before the LEDs draw any current.
void test_a_low_battery_at_power_on_is_low_at_once(void)
{
    TEST_ASSERT_TRUE(check_at(0, BATTERY_LOW_MV - 50));
}


void test_readings_move_1_minus_1_over_e_of_the_way_in_the_smoothing_time(void)
{
    check_at(0, 4000);
    check_at(BATTERY_SMOOTHING_US, 3000);

    TEST_ASSERT_FLOAT_WITHIN(1, 3000 + 1000 / M_E, battery_mv);
}


// The LEDs' current drags the voltage down with the music.
void test_one_dip_with_the_music_doesnt_make_the_battery_low(void)
{
    check_at(0, 3700);

    TEST_ASSERT_FALSE(check_at(CHECK_US, 3400));
    TEST_ASSERT_FALSE(check_at(2 * CHECK_US, 3700));
}


void test_a_battery_that_stays_under_3600mv_is_low_within_a_minute(void)
{
    bool low = check_at(0, 3700);

    for (int64_t t = CHECK_US; t <= 60000000; t += CHECK_US)
        low = check_at(t, 3500);

    TEST_ASSERT_TRUE(low);
}


// So the warning doesn't flicker on and off around 3.6 V.
void test_a_low_battery_stays_low_until_it_charges_over_3700mv(void)
{
    int64_t t = 0;
    check_at(t, 3500);

    // Ten minutes at 3.65 V.
    for (int i = 0; i < 60; i++) {
        t += CHECK_US;
        TEST_ASSERT_TRUE(check_at(t, 3650));
    }

    bool low = true;
    for (int i = 0; i < 6; i++) {
        t += CHECK_US;
        low = check_at(t, 3800);
    }
    TEST_ASSERT_FALSE(low);
}


// On v1.2 the divider is after the power switch, so with USB plugged in it
// reads the USB supply, a little under 5 V, rather than the battery.
void test_plugging_in_usb_on_v1_2_ends_low_at_the_next_check(void)
{
    TEST_ASSERT_TRUE(check_at(0, 3500));

    TEST_ASSERT_FALSE(check_at(CHECK_US, 4600));
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_reads_gpio_0_on_adc1);
    RUN_TEST(test_a_reading_is_twice_the_voltage_at_gpio_0);
    RUN_TEST(test_a_reading_without_calibration_assumes_3100mv_full_scale);
    RUN_TEST(test_a_full_battery_is_not_low);
    RUN_TEST(test_a_low_battery_at_power_on_is_low_at_once);
    RUN_TEST(test_readings_move_1_minus_1_over_e_of_the_way_in_the_smoothing_time);
    RUN_TEST(test_one_dip_with_the_music_doesnt_make_the_battery_low);
    RUN_TEST(test_a_battery_that_stays_under_3600mv_is_low_within_a_minute);
    RUN_TEST(test_a_low_battery_stays_low_until_it_charges_over_3700mv);
    RUN_TEST(test_plugging_in_usb_on_v1_2_ends_low_at_the_next_check);
    return UNITY_END();
}
