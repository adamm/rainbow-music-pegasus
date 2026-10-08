// Tests for main.c's signal path, from a frame of mic samples to LED
// brightnesses, and for how it reads the battery.
//
// This includes main.c, rather than linking it, to reach its static functions,
// and replaces mic.c and battery.c with the fakes below, so each test chooses
// what the mic hears and whether the battery is low.
#include "main.c"

#include "fake_idf.h"
#include "unity.h"

#define MAX_SAMPLES 512

// A bin that every board shows, clear of DC.
#define TONE_BIN 5

// The fake mic.  mic_read_frame() hands out the tone set by hear_tone(), one
// sample after another, so reads join up as they do from the ADC, and
// heard_samples counts the samples handed out.  mic_sensitivity_update()
// records whether main.c found the frame too loud or too quiet, and reports a
// gain change of gain_change.  mic_frames_dropped() and mic_time_waited_us()
// report dropped_frames and waited_us, and each mic_read_frame() waits
// read_wait_us for the samples.
static int heard_bin;
static double heard_mv;
static long heard_samples;
static bool heard_clipped;
static bool reported_loud;
static bool reported_quiet;
static float gain_change;
static uint32_t dropped_frames;
static int64_t waited_us;
static int64_t read_wait_us;

void mic_init(int frame_samples)
{
}

void mic_stop(void)
{
}

// The sample of the heard tone the fake mic hands out i samples after the
// first: amplitude heard_mv, around the middle of the ADC's range, in the
// middle of FFT bin heard_bin.
static int heard_sample(long i)
{
    return 1650 + (int)lround(heard_mv * sin(2 * M_PI * heard_bin * i / N_SAMPLES));
}

bool mic_read_frame(int* voltages, int total_samples)
{
    fake.now_us += read_wait_us;
    waited_us += read_wait_us;
    for (int i = 0; i < total_samples; i++)
        voltages[i] = heard_sample(heard_samples++);
    return heard_clipped;
}

float mic_sensitivity_update(bool loud, bool quiet)
{
    reported_loud = loud;
    reported_quiet = quiet;
    return gain_change;
}

uint32_t mic_frames_dropped(void)
{
    return dropped_frames;
}

int64_t mic_time_waited_us(void)
{
    return waited_us;
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
static float vDecay[MAX_SAMPLES];
static float vFloor[MAX_SAMPLES];
static uint8_t colours[MAX_SAMPLES];

// Set every bin's floor to mv.
static void set_floor(float mv)
{
    for (int i = 0; i < MAX_SAMPLES; i++)
        vFloor[i] = mv;
}

// A crowd's noise, as an xorshift generator's state, so every run hears the
// same crowd.
static uint32_t crowd_state;

// Set up for a board with `leds` LEDs showing `pattern`, which config_init()
// gives an FFT of `samples`.
static void use_board(int leds, config_pattern_t pattern, int samples)
{
    _config_total_leds = leds;
    _config_pattern = pattern;
    _config_total_samples = samples;
    fft_init(samples);
    frame_stale = true;  // as at power-on
    heard_samples = 0;
}

void setUp(void)
{
    fake_idf_reset();
    memset(vDecay, 0, sizeof(vDecay));
    set_floor(FLOOR_LOWEST_MV);  // as app_main() starts
    floor_logged_us = 0;
    memset(&frame_stats, 0, sizeof(frame_stats));
    dropped_frames = 0;
    frame_dropped = 0;
    waited_us = 0;
    read_wait_us = 0;
    crowd_state = 1;
    gain_change = 1;
    heard_clipped = false;
    mic_paused = false;
    checked_with_mic_paused = false;
    use_board(10, CONFIG_PATTERN_A, 64);
}

void tearDown(void)
{
}


// The mic hears a tone of amplitude_mv, around the middle of the ADC's range,
// in the middle of FFT bin `bin`.  What frequency that is depends on the
// board's sample rate, which the signal path doesn't use.
static void hear_tone(int bin, double amplitude_mv)
{
    heard_bin = bin;
    heard_mv = amplitude_mv;
}


// Check the frame in voltages is the newest N_SAMPLES the mic handed out.
static void assert_frame_is_the_newest_samples(void)
{
    for (int i = 0; i < N_SAMPLES; i++) {
        char message[32];
        snprintf(message, sizeof(message), "sample %d of the frame", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(heard_sample(heard_samples - N_SAMPLES + i), voltages[i], message);
    }
}


// So the LEDs update every half frame, and follow quick changes in a held
// note without flickering.
void test_each_frame_keeps_the_newest_half_of_the_last_and_reads_the_next_half(void)
{
    use_board(24, CONFIG_PATTERN_C, 512);
    hear_tone(TONE_BIN, 100);

    read_spectrum(voltages, vReal);
    TEST_ASSERT_EQUAL(512, heard_samples);

    read_spectrum(voltages, vReal);
    TEST_ASSERT_EQUAL(512 + 256, heard_samples);
    assert_frame_is_the_newest_samples();
    TEST_ASSERT_FLOAT_WITHIN(3, 100, vReal[TONE_BIN]);
}


// The samples kept from the last frame end before the pause.
void test_after_a_battery_check_the_next_frame_is_read_whole(void)
{
    read_spectrum(voltages, vReal);
    read_battery();
    read_spectrum(voltages, vReal);

    TEST_ASSERT_EQUAL(2 * N_SAMPLES, heard_samples);
    assert_frame_is_the_newest_samples();
}


// A dropped hop leaves a gap after the samples kept from the last frame.
void test_after_the_mic_drops_a_hop_the_next_frame_is_read_whole(void)
{
    read_spectrum(voltages, vReal);
    dropped_frames++;
    read_spectrum(voltages, vReal);

    TEST_ASSERT_EQUAL(2 * N_SAMPLES, heard_samples);
    assert_frame_is_the_newest_samples();

    read_spectrum(voltages, vReal);
    TEST_ASSERT_EQUAL(2 * N_SAMPLES + N_HOP, heard_samples);
}


// So the LEDs behave the same whatever the frame size.
void test_a_tone_reads_as_its_amplitude_in_mv_at_every_frame_size(void)
{
    static const struct {
        int leds;
        config_pattern_t pattern;
        int samples;
    } boards[] = {
        { 10, CONFIG_PATTERN_A,  64 },
        { 16, CONFIG_PATTERN_A, 128 },
        { 24, CONFIG_PATTERN_A, 256 },
        { 24, CONFIG_PATTERN_B, 512 },
    };

    for (int i = 0; i < sizeof(boards) / sizeof(boards[0]); i++) {
        use_board(boards[i].leds, boards[i].pattern, boards[i].samples);
        hear_tone(TONE_BIN, 100);

        read_spectrum(voltages, vReal);

        char message[32];
        snprintf(message, sizeof(message), "%d-sample frame", boards[i].samples);
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(3, 100, vReal[TONE_BIN], message);
    }
}


void test_a_tone_leaves_shown_bins_away_from_it_under_the_noise_floor(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV);

    read_spectrum(voltages, vReal);

    for (int i = 0; i < N_DISPLAYED_BINS; i++) {
        if (abs(i - TONE_BIN) > 3)
            TEST_ASSERT_LESS_THAN_FLOAT(FFT_NOISE_FLOOR_MV, vReal[i]);
    }
}


void test_silence_is_too_quiet(void)
{
    hear_tone(TONE_BIN, 0);

    read_spectrum(voltages, vReal);

    TEST_ASSERT_FALSE(reported_loud);
    TEST_ASSERT_TRUE(reported_quiet);
}


void test_a_tone_over_full_scale_is_too_loud(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV * 1.1);

    read_spectrum(voltages, vReal);

    TEST_ASSERT_TRUE(reported_loud);
    TEST_ASSERT_FALSE(reported_quiet);
}


void test_a_tone_between_a_quarter_and_full_scale_is_neither(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV / 2);

    read_spectrum(voltages, vReal);

    TEST_ASSERT_FALSE(reported_loud);
    TEST_ASSERT_FALSE(reported_quiet);
}


void test_clipping_is_too_loud_even_when_the_bins_are_quiet(void)
{
    hear_tone(TONE_BIN, 0);
    heard_clipped = true;

    read_spectrum(voltages, vReal);

    TEST_ASSERT_TRUE(reported_loud);
}


// 10 LEDs show bins 0 to 14 in pattern A, of a 64-sample FFT, and bins 0 to
// 29 in patterns B and C, of a 128-sample FFT.
static const struct {
    config_pattern_t pattern;
    int samples;
} ten_led_boards[] = {
    { CONFIG_PATTERN_A,  64 },
    { CONFIG_PATTERN_B, 128 },
    { CONFIG_PATTERN_C, 128 },
};
#define N_TEN_LED_BOARDS (sizeof(ten_led_boards) / sizeof(ten_led_boards[0]))


void test_tones_above_the_highest_led_are_ignored(void)
{
    for (int i = 0; i < N_TEN_LED_BOARDS; i++) {
        use_board(10, ten_led_boards[i].pattern, ten_led_boards[i].samples);
        hear_tone(N_DISPLAYED_BINS + 10, FFT_FULL_SCALE_MV * 2);

        read_spectrum(voltages, vReal);

        char message[16];
        snprintf(message, sizeof(message), "pattern %c", 'A' + ten_led_boards[i].pattern);
        TEST_ASSERT_FALSE_MESSAGE(reported_loud, message);
        TEST_ASSERT_TRUE_MESSAGE(reported_quiet, message);
    }
}


// So the mic's gain fits the brightest LED, wherever it is.
void test_a_tone_in_the_highest_led_counts_in_every_pattern(void)
{
    for (int i = 0; i < N_TEN_LED_BOARDS; i++) {
        use_board(10, ten_led_boards[i].pattern, ten_led_boards[i].samples);
        hear_tone(N_DISPLAYED_BINS - 1, FFT_FULL_SCALE_MV * 1.1);

        read_spectrum(voltages, vReal);

        char message[16];
        snprintf(message, sizeof(message), "pattern %c", 'A' + ten_led_boards[i].pattern);
        TEST_ASSERT_TRUE_MESSAGE(reported_loud, message);
    }
}


// So app_main() can rescale the floor to match.
void test_reading_a_frame_returns_how_much_the_mic_gain_changed(void)
{
    hear_tone(TONE_BIN, FFT_FULL_SCALE_MV * 1.1);
    gain_change = 0.75f;

    TEST_ASSERT_EQUAL_FLOAT(0.75f, read_spectrum(voltages, vReal));
}


// Run spectrum_to_colours() on a frame with every bin at mv, elapsed_us after
// the last one.
static void colour_frame(float mv, int64_t elapsed_us)
{
    for (int i = 0; i < N_SAMPLES; i++)
        vReal[i] = mv;
    spectrum_to_colours(vReal, vFloor, vDecay, colours, elapsed_us);
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


// Learn the floor from `seconds` of 10 ms frames with every bin at mv,
// speedup times faster than normal.
static void hold_bins_at(float mv, double seconds, float speedup)
{
    for (int i = 0; i < N_SAMPLES; i++)
        vReal[i] = mv;
    for (long frame = 0; frame < lround(seconds * 100); frame++)
        track_floor(vReal, vFloor, 1, 10000, speedup);
}


void test_the_floor_rises_slowly_under_a_louder_bin(void)
{
    set_floor(10);
    hold_bins_at(100, 1, 1);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10 * DB_TO_RATIO(FLOOR_RISE_DB_PER_S), vFloor[0]);
}


void test_the_floor_falls_faster_over_a_quieter_bin(void)
{
    set_floor(10);
    hold_bins_at(1, 1, 1);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10 * DB_TO_RATIO(-FLOOR_FALL_DB_PER_S), vFloor[0]);
}


void test_the_floor_learns_faster_while_the_mic_settles(void)
{
    set_floor(10);
    hold_bins_at(100, 1, FLOOR_SETTLE_SPEEDUP);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 10 * DB_TO_RATIO(FLOOR_RISE_DB_PER_S * FLOOR_SETTLE_SPEEDUP), vFloor[0]);
}


// So it learns a new crowd from where it starts to matter, and hiss stays dark
// as it always has.
void test_in_silence_the_floor_rests_at_the_fixed_noise_floor(void)
{
    set_floor(50);
    hold_bins_at(0, 60, 1);

    TEST_ASSERT_EQUAL_FLOAT(FLOOR_LOWEST_MV, vFloor[0]);
    TEST_ASSERT_EQUAL(0, brightness(FFT_NOISE_FLOOR_MV));
    TEST_ASSERT_GREATER_THAN(0, brightness(2 * FFT_NOISE_FLOOR_MV));
}


void test_the_floor_follows_a_mic_gain_change(void)
{
    set_floor(40);
    track_floor(vReal, vFloor, 0.75f, 0, 1);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30, vFloor[0]);
}


void test_bins_up_to_the_margin_over_their_floor_are_dark(void)
{
    set_floor(20);
    float threshold = 20 * DB_TO_RATIO(FLOOR_MARGIN_DB);

    TEST_ASSERT_EQUAL(0, brightness(threshold));
    TEST_ASSERT_GREATER_THAN(0, brightness(threshold * 1.05f));
}


// So music over a noisy room isn't dimmed by it.
void test_a_bin_over_a_raised_floor_still_reaches_full_brightness(void)
{
    set_floor(20);
    float threshold = 20 * DB_TO_RATIO(FLOOR_MARGIN_DB);

    TEST_ASSERT_EQUAL(250, brightness(FFT_FULL_SCALE_MV));
    TEST_ASSERT_INT_WITHIN(1, 125, brightness((threshold + FFT_FULL_SCALE_MV) / 2));
}


// Learn the floor from the bins in vReal, then turn them into brightnesses, as
// app_main() does for a 10 ms frame.
static void show_frame(float speedup)
{
    track_floor(vReal, vFloor, 1, 10000, speedup);
    spectrum_to_colours(vReal, vFloor, vDecay, colours, 10000);
}

// A crowd whose average level in each bin is 20 mV.  Many voices add up to
// noise, so each bin's level in a frame is Rayleigh distributed around that.
#define CROWD_MV 20.0f

static void hear_crowd(void)
{
    for (int i = 0; i < N_SAMPLES; i++) {
        crowd_state ^= crowd_state << 13;
        crowd_state ^= crowd_state >> 17;
        crowd_state ^= crowd_state << 5;
        double uniform = (crowd_state + 0.5) / 4294967296.0;
        vReal[i] = CROWD_MV / 1.2533f * sqrtf(-2 * logf((float)uniform));
    }
}

// Power on in a crowded room, and let the floor learn it for a minute.
static void learn_crowd(void)
{
    for (int frame = 0; frame < 300; frame++) {
        hear_crowd();
        show_frame(FLOOR_SETTLE_SPEEDUP);
    }
    for (int frame = 0; frame < 6000; frame++) {
        hear_crowd();
        show_frame(1);
    }
}


void test_a_steady_crowd_rarely_lights_an_led(void)
{
    learn_crowd();

    int lit = 0, shown = 0;
    for (int frame = 0; frame < 6000; frame++) {
        hear_crowd();
        show_frame(1);
        for (int i = 0; i < N_DISPLAYED_BINS; i++, shown++)
            lit += vReal[i] > 0;
    }

    TEST_ASSERT_LESS_THAN_FLOAT(0.02f, (float)lit / shown);
}


void test_beats_12_db_over_a_crowd_light_every_led(void)
{
    learn_crowd();

    // A 100 ms beat every 500 ms (120 beats per minute) at 4 times the crowd's
    // average level shows at about a quarter of full brightness.
    for (int frame = 0; frame < 3000; frame++) {
        bool beat = frame % 50 < 10;
        hear_crowd();
        if (beat) {
            for (int i = 0; i < N_SAMPLES; i++)
                vReal[i] = 4 * CROWD_MV;
        }
        show_frame(1);

        for (int i = 0; beat && i < N_DISPLAYED_BINS; i++) {
            char message[32];
            snprintf(message, sizeof(message), "frame %d bin %d", frame, i);
            TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(50, (int)vReal[i], message);
        }
    }
}


// The cost of learning the crowd: a sound that doesn't change is learned too.
void test_a_held_note_fades_into_the_background(void)
{
    for (int frame = 0; frame < 1000; frame++) {
        for (int i = 0; i < N_SAMPLES; i++)
            vReal[i] = 100;
        show_frame(1);
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, (int)vReal[0], "after 10 s");

    for (int frame = 0; frame < 2000; frame++) {
        for (int i = 0; i < N_SAMPLES; i++)
            vReal[i] = 100;
        show_frame(1);
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, (int)vReal[0], "after 30 s");
}


void test_the_floor_log_counts_the_bins_whose_floor_moved(void)
{
    int64_t t = fake.now_us;
    set_floor(10);
    log_floor(vFloor, 1, t);
    TEST_ASSERT_EQUAL_STRING("", fake.last_log);

    vFloor[0] = vFloor[1] = vFloor[2] = 20;
    vFloor[3] = vFloor[4] = 5;
    vFloor[5] = 10 * DB_TO_RATIO(FLOOR_LOG_MOVED_DB / 2);  // jitter
    log_floor(vFloor, 1, t + FLOOR_LOG_US);

    TEST_ASSERT_EQUAL_STRING("main: adjusted the floor up on 3 bins and down on 2, of 15", fake.last_log);
}


void test_the_floor_log_waits_between_lines(void)
{
    int64_t t = fake.now_us;
    set_floor(10);
    log_floor(vFloor, 1, t);

    set_floor(20);
    log_floor(vFloor, 1, t + FLOOR_LOG_US - 1);
    TEST_ASSERT_EQUAL_STRING("", fake.last_log);

    log_floor(vFloor, 1, t + FLOOR_LOG_US);
    TEST_ASSERT_EQUAL_STRING("main: adjusted the floor up on 15 bins and down on 0, of 15", fake.last_log);
}


// mic.c logs those itself, and they say nothing about the room.
void test_the_floor_log_leaves_out_mic_gain_changes(void)
{
    int64_t t = fake.now_us;
    set_floor(10);
    log_floor(vFloor, 1, t);

    track_floor(vReal, vFloor, 0.75f, 0, 1);
    log_floor(vFloor, 0.75f, t + FLOOR_LOG_US);

    TEST_ASSERT_EQUAL_STRING("main: adjusted the floor up on 0 bins and down on 0, of 15", fake.last_log);
}


// 24 LEDs show the most bins, in patterns B and C.
void test_the_floor_log_counts_all_72_bins_of_24_leds_in_pattern_b(void)
{
    use_board(24, CONFIG_PATTERN_B, 512);
    int64_t t = fake.now_us;
    set_floor(10);
    log_floor(vFloor, 1, t);

    set_floor(20);
    log_floor(vFloor, 1, t + FLOOR_LOG_US);

    TEST_ASSERT_EQUAL_STRING("main: adjusted the floor up on 72 bins and down on 0, of 72", fake.last_log);
}


// End a frame of the light show that was busy for busy_us, after waiting
// wait_us for the mic.
static void frame_ended_after(int64_t busy_us, int64_t wait_us)
{
    waited_us += wait_us;
    fake.now_us += busy_us + wait_us;
    log_frames(fake.now_us);
}


// So the serial log shows whether the main loop keeps up with the mic.
void test_the_frame_log_reports_how_long_frames_took_and_how_many_were_dropped(void)
{
    _config_sample_freq_hz = 10000;  // 64-sample frames last 6.4 ms
    start_frame_log(fake.now_us);

    frame_ended_after(4000, 2400);
    dropped_frames = 1;
    frame_ended_after(2000, FRAME_LOG_US - 8400 - 1);
    TEST_ASSERT_EQUAL_STRING("", fake.last_log);

    frame_ended_after(3000, 1);
    TEST_ASSERT_EQUAL_STRING("main: 3 frames of 6.4 ms, 3.2 ms apart, 1 dropped, 3.0 ms busy each (max 4.0): "
                             "read 0.0, window 0.0, FFT 0.0, magnitude 0.0, rest 3.0", fake.last_log);

    // The next line counts from there.
    fake.last_log[0] = '\0';
    frame_ended_after(5000, 1000);
    frame_ended_after(3000, FRAME_LOG_US - 9000);
    TEST_ASSERT_EQUAL_STRING("main: 2 frames of 6.4 ms, 3.2 ms apart, 0 dropped, 4.0 ms busy each (max 5.0): "
                             "read 0.0, window 0.0, FFT 0.0, magnitude 0.0, rest 4.0", fake.last_log);
}


// Waiting for the mic isn't time spent reading a frame.
void test_the_frame_log_leaves_waiting_for_the_mic_out_of_reading_a_frame(void)
{
    start_frame_log(fake.now_us);
    hear_tone(TONE_BIN, 0);
    read_wait_us = 6400;

    read_spectrum(voltages, vReal);

    TEST_ASSERT_EQUAL_INT64(0, frame_stats.read_us);
    TEST_ASSERT_EQUAL_INT64(0, frame_stats.window_us);
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
    RUN_TEST(test_each_frame_keeps_the_newest_half_of_the_last_and_reads_the_next_half);
    RUN_TEST(test_after_a_battery_check_the_next_frame_is_read_whole);
    RUN_TEST(test_after_the_mic_drops_a_hop_the_next_frame_is_read_whole);
    RUN_TEST(test_a_tone_reads_as_its_amplitude_in_mv_at_every_frame_size);
    RUN_TEST(test_a_tone_leaves_shown_bins_away_from_it_under_the_noise_floor);
    RUN_TEST(test_silence_is_too_quiet);
    RUN_TEST(test_a_tone_over_full_scale_is_too_loud);
    RUN_TEST(test_a_tone_between_a_quarter_and_full_scale_is_neither);
    RUN_TEST(test_clipping_is_too_loud_even_when_the_bins_are_quiet);
    RUN_TEST(test_tones_above_the_highest_led_are_ignored);
    RUN_TEST(test_a_tone_in_the_highest_led_counts_in_every_pattern);
    RUN_TEST(test_reading_a_frame_returns_how_much_the_mic_gain_changed);
    RUN_TEST(test_bins_at_or_under_the_noise_floor_are_dark);
    RUN_TEST(test_bins_at_or_over_full_scale_are_full_brightness);
    RUN_TEST(test_brightness_is_linear_in_between);
    RUN_TEST(test_a_louder_bin_lights_its_led_at_once);
    RUN_TEST(test_a_quieter_bin_fades_to_1_over_e_in_the_decay_time);
    RUN_TEST(test_one_long_frame_fades_as_much_as_several_short_ones);
    RUN_TEST(test_the_floor_rises_slowly_under_a_louder_bin);
    RUN_TEST(test_the_floor_falls_faster_over_a_quieter_bin);
    RUN_TEST(test_the_floor_learns_faster_while_the_mic_settles);
    RUN_TEST(test_in_silence_the_floor_rests_at_the_fixed_noise_floor);
    RUN_TEST(test_the_floor_follows_a_mic_gain_change);
    RUN_TEST(test_bins_up_to_the_margin_over_their_floor_are_dark);
    RUN_TEST(test_a_bin_over_a_raised_floor_still_reaches_full_brightness);
    RUN_TEST(test_a_steady_crowd_rarely_lights_an_led);
    RUN_TEST(test_beats_12_db_over_a_crowd_light_every_led);
    RUN_TEST(test_a_held_note_fades_into_the_background);
    RUN_TEST(test_the_floor_log_counts_the_bins_whose_floor_moved);
    RUN_TEST(test_the_floor_log_waits_between_lines);
    RUN_TEST(test_the_floor_log_leaves_out_mic_gain_changes);
    RUN_TEST(test_the_floor_log_counts_all_72_bins_of_24_leds_in_pattern_b);
    RUN_TEST(test_the_frame_log_reports_how_long_frames_took_and_how_many_were_dropped);
    RUN_TEST(test_the_frame_log_leaves_waiting_for_the_mic_out_of_reading_a_frame);
    RUN_TEST(test_the_battery_is_read_with_the_mic_paused);
    return UNITY_END();
}
