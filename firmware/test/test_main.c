// Tests for main.c's signal path, from a frame of mic samples to LED
// brightnesses, and for how it reads the battery.
//
// This includes main.c, rather than linking it, to reach its static functions,
// and replaces mic.c and battery.c with the fakes below, so each test chooses
// what the mic hears and whether the battery is low.
#include "main.c"

#include "fake_idf.h"
#include "unity.h"

#define MAX_SAMPLES 256

// A bin that every board shows, clear of DC.
#define TONE_BIN 5

// The fake mic.  mic_read_frame() returns heard_mv[], and
// mic_sensitivity_update() records whether main.c found the frame too loud or
// too quiet.
static int heard_mv[MAX_SAMPLES];
static bool heard_clipped;
static bool reported_loud;
static bool reported_quiet;

void mic_init(void)
{
}

void mic_stop(void)
{
}

bool mic_read_frame(int* voltages, int total_samples)
{
    memcpy(voltages, heard_mv, total_samples * sizeof(int));
    return heard_clipped;
}

void mic_sensitivity_update(bool loud, bool quiet)
{
    reported_loud = loud;
    reported_quiet = quiet;
}

static bool mic_paused;

void mic_pause(void)
{
    TEST_ASSERT_FALSE_MESSAGE(mic_paused, "Mic paused twice");
    mic_paused = true;
}

void mic_resume(void)
{
    TEST_ASSERT_TRUE_MESSAGE(mic_paused, "Mic resumed without a pause");
    mic_paused = false;
}

// The fake battery.  battery_check() records whether the mic was paused, as
// ADC1 can't read the battery while it samples the mic.
static bool checked_with_mic_paused;

void battery_init(void)
{
}

void battery_stop(void)
{
}

bool battery_check(void)
{
    checked_with_mic_paused = mic_paused;
    return false;
}

static int voltages[MAX_SAMPLES];
static float vReal[MAX_SAMPLES];
static float vImag[MAX_SAMPLES];
static float vDecay[MAX_SAMPLES];
static uint8_t colours[MAX_SAMPLES];

// Set up for a board with `leds` LEDs, which config_init() gives an FFT of
// `samples`.
static void use_board(int leds, int samples)
{
    _config_total_leds = leds;
    _config_total_samples = samples;
    fft_init(vReal, vImag, samples, _config_sample_freq_hz);
}

void setUp(void)
{
    fake_idf_reset();
    memset(vDecay, 0, sizeof(vDecay));
    heard_clipped = false;
    mic_paused = false;
    checked_with_mic_paused = false;
    use_board(10, 64);
}

void tearDown(void)
{
}


// The mic hears a tone of amplitude_mv, around the middle of the ADC's range,
// in the middle of FFT bin `bin`.  What frequency that is depends on the
// board's sample rate, which the signal path doesn't use.
static void hear_tone(int bin, double amplitude_mv)
{
    for (int i = 0; i < MAX_SAMPLES; i++)
        heard_mv[i] = 1650 + (int)lround(amplitude_mv * sin(2 * M_PI * bin * i / N_SAMPLES));
}


// So the LEDs behave the same whatever the frame size.
void test_a_tone_reads_as_its_amplitude_in_mv_at_every_frame_size(void)
{
    static const struct {
        int leds;
        int samples;
    } boards[] = { { 10, 64 }, { 16, 128 }, { 24, 256 } };

    for (int i = 0; i < sizeof(boards) / sizeof(boards[0]); i++) {
        use_board(boards[i].leds, boards[i].samples);
        hear_tone(TONE_BIN, 100);

        read_spectrum(voltages, vReal, vImag);

        char message[32];
        snprintf(message, sizeof(message), "%d-sample frame", boards[i].samples);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(3, 100, vReal[TONE_BIN], message);
    }
}


void test_a_tone_leaves_bins_away_from_it_under_the_noise_floor(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV);

    read_spectrum(voltages, vReal, vImag);

    for (int i = 0; i < N_SAMPLES / 2; i++) {
        if (abs(i - TONE_BIN) > 3)
            TEST_ASSERT_LESS_THAN_FLOAT(FFT_NOISE_FLOOR_MV, vReal[i]);
    }
}


void test_silence_is_too_quiet(void)
{
    hear_tone(TONE_BIN, 0);

    read_spectrum(voltages, vReal, vImag);

    TEST_ASSERT_FALSE(reported_loud);
    TEST_ASSERT_TRUE(reported_quiet);
}


void test_a_tone_over_full_scale_is_too_loud(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV * 1.1);

    read_spectrum(voltages, vReal, vImag);

    TEST_ASSERT_TRUE(reported_loud);
    TEST_ASSERT_FALSE(reported_quiet);
}


void test_a_tone_between_a_quarter_and_full_scale_is_neither(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV / 2);

    read_spectrum(voltages, vReal, vImag);

    TEST_ASSERT_FALSE(reported_loud);
    TEST_ASSERT_FALSE(reported_quiet);
}


void test_clipping_is_too_loud_even_when_the_bins_are_quiet(void)
{
    hear_tone(TONE_BIN, 0);
    heard_clipped = true;

    read_spectrum(voltages, vReal, vImag);

    TEST_ASSERT_TRUE(reported_loud);
}


void test_tones_above_the_highest_led_are_ignored(void)
{
    // 10 LEDs show bins 0 to 14.
    hear_tone(25, FFT_FULL_SCALE_MV * 2);

    read_spectrum(voltages, vReal, vImag);

    TEST_ASSERT_FALSE(reported_loud);
    TEST_ASSERT_TRUE(reported_quiet);
}


// Run spectrum_to_colours() on a frame with every bin at mv, elapsed_us after
// the last one.
static void colour_frame(float mv, int64_t elapsed_us)
{
    for (int i = 0; i < N_SAMPLES; i++)
        vReal[i] = mv;
    spectrum_to_colours(vReal, vDecay, colours, elapsed_us);
}

// How brightly a dark LED lights for a bin at mv.
static int brightness(float mv)
{
    memset(vDecay, 0, sizeof(vDecay));
    colour_frame(mv, 0);
    return colours[0];
}


void test_bins_at_or_under_the_noise_floor_are_dark(void)
{
    TEST_ASSERT_EQUAL(0, brightness(0));
    TEST_ASSERT_EQUAL(0, brightness(FFT_NOISE_FLOOR_MV));
}


void test_bins_at_or_over_full_scale_are_full_brightness(void)
{
    TEST_ASSERT_EQUAL(250, brightness(FFT_FULL_SCALE_MV));
    TEST_ASSERT_EQUAL(250, brightness(10 * FFT_FULL_SCALE_MV));
}


void test_brightness_is_linear_in_between(void)
{
    TEST_ASSERT_INT_WITHIN(1, 125, brightness((FFT_NOISE_FLOOR_MV + FFT_FULL_SCALE_MV) / 2));
}


void test_a_louder_bin_lights_its_led_at_once(void)
{
    colour_frame(FFT_NOISE_FLOOR_MV, 0);
    colour_frame(FFT_FULL_SCALE_MV, 6400);

    TEST_ASSERT_EQUAL(250, colours[0]);
}


void test_a_quieter_bin_fades_to_1_over_e_in_the_decay_time(void)
{
    colour_frame(FFT_FULL_SCALE_MV, 0);
    colour_frame(0, LED_DECAY_US);

    TEST_ASSERT_INT_WITHIN(1, (int)(250 / M_E), colours[0]);
}


// So the fade doesn't depend on the frame size, or on frames the driver
// dropped.
void test_one_long_frame_fades_as_much_as_several_short_ones(void)
{
    colour_frame(FFT_FULL_SCALE_MV, 0);
    colour_frame(0, 25600);
    float after_one_frame = vDecay[0];

    colour_frame(FFT_FULL_SCALE_MV, 0);
    for (int i = 0; i < 4; i++)
        colour_frame(0, 6400);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, after_one_frame, vDecay[0]);
}


void test_the_battery_is_read_with_the_mic_paused(void)
{
    read_battery();

    TEST_ASSERT_TRUE(checked_with_mic_paused);
    TEST_ASSERT_FALSE(mic_paused);
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_tone_reads_as_its_amplitude_in_mv_at_every_frame_size);
    RUN_TEST(test_a_tone_leaves_bins_away_from_it_under_the_noise_floor);
    RUN_TEST(test_silence_is_too_quiet);
    RUN_TEST(test_a_tone_over_full_scale_is_too_loud);
    RUN_TEST(test_a_tone_between_a_quarter_and_full_scale_is_neither);
    RUN_TEST(test_clipping_is_too_loud_even_when_the_bins_are_quiet);
    RUN_TEST(test_tones_above_the_highest_led_are_ignored);
    RUN_TEST(test_bins_at_or_under_the_noise_floor_are_dark);
    RUN_TEST(test_bins_at_or_over_full_scale_are_full_brightness);
    RUN_TEST(test_brightness_is_linear_in_between);
    RUN_TEST(test_a_louder_bin_lights_its_led_at_once);
    RUN_TEST(test_a_quieter_bin_fades_to_1_over_e_in_the_decay_time);
    RUN_TEST(test_one_long_frame_fades_as_much_as_several_short_ones);
    RUN_TEST(test_the_battery_is_read_with_the_mic_paused);
    return UNITY_END();
}
