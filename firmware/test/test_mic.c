// Tests for reading the mic and adjusting its sensitivity.
//
// This includes mic.c, rather than linking it, so each test can start from a
// freshly booted board: mic_init() can only run once per boot.
#include "mic.c"

#include "fake_idf.h"
#include "unity.h"

#define FRAME_SAMPLES   64
#define MIC             CONFIG_MIC_CHANNEL

static int64_t start_us;

// Put mic.c back how it is at boot, then start it up as app_main() does.
static void boot(void)
{
    free(mic_frame);
    mic_frame = NULL;
    mic_calibrated = false;
    mic_cali_channel_handle = NULL;
    mic_sensitivity = 0;
    mic_sensitivity_update(false, false);  // clears its hold timers
    fake.adc_running = false;

    _config_total_samples = FRAME_SAMPLES;
    digipot_init();
    mic_init();
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


// The wiper code last sent to the digipot, which sets the preamp gain.
static int wiper(void)
{
    return fake.spi_sent.tx_data[1];
}

typedef enum { NORMAL, LOUD, QUIET, LOUD_AND_QUIET } frame_t;

// Report a frame to mic_sensitivity_update(), as main.c does after each FFT,
// t_us into the test.
static void frame_at(int64_t t_us, frame_t frame)
{
    fake.now_us = start_us + t_us;
    mic_sensitivity_update(frame == LOUD || frame == LOUD_AND_QUIET,
                           frame == QUIET || frame == LOUD_AND_QUIET);
}


void test_init_samples_the_mic_at_10khz_keeping_only_the_newest_frame(void)
{
    TEST_ASSERT_TRUE(fake.adc_running);
    TEST_ASSERT_EQUAL(10000, fake.adc_config.sample_freq_hz);
    TEST_ASSERT_EQUAL(ADC_UNIT_1, fake.adc_pattern.unit);
    TEST_ASSERT_EQUAL(ADC_CHANNEL_4, fake.adc_pattern.channel);  // GPIO 4
    TEST_ASSERT_EQUAL(ADC_ATTEN_DB_12, fake.adc_pattern.atten);

    // So the LEDs never lag the sound by more than a frame.
    TEST_ASSERT_EQUAL(FRAME_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES, fake.adc_handle_config.conv_frame_size);
    TEST_ASSERT_EQUAL(FRAME_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES, fake.adc_handle_config.max_store_buf_size);
    TEST_ASSERT_TRUE(fake.adc_handle_config.flags.flush_pool);
}


void test_read_frame_returns_each_sample_in_mv(void)
{
    for (int i = 0; i < FRAME_SAMPLES; i++)
        fake_adc_queue(MIC, 1600 + i);

    int mv[FRAME_SAMPLES];
    mic_read_frame(mv, FRAME_SAMPLES);

    for (int i = 0; i < FRAME_SAMPLES; i++)
        TEST_ASSERT_EQUAL(1600 + i, mv[i]);
}


void test_read_frame_skips_results_from_other_channels(void)
{
    fake_adc_queue(MIC, 1650);
    fake_adc_queue(ADC_CHANNEL_7, 4095);
    fake_adc_queue(MIC, 1651);

    int mv[2];
    mic_read_frame(mv, 2);

    TEST_ASSERT_EQUAL(1650, mv[0]);
    TEST_ASSERT_EQUAL(1651, mv[1]);
}


void test_read_frame_keeps_reading_until_the_frame_is_full(void)
{
    fake.adc_read_chunk = 10;
    for (int i = 0; i < FRAME_SAMPLES; i++)
        fake_adc_queue(MIC, 1600 + i);

    int mv[FRAME_SAMPLES];
    mic_read_frame(mv, FRAME_SAMPLES);

    for (int i = 0; i < FRAME_SAMPLES; i++)
        TEST_ASSERT_EQUAL(1600 + i, mv[i]);
}


void test_read_frame_reads_no_further_than_the_frame(void)
{
    // A bad result makes the first read one sample short.
    fake_adc_queue(ADC_CHANNEL_7, 4095);
    for (int i = 0; i < 2 * FRAME_SAMPLES; i++)
        fake_adc_queue(MIC, 1650);

    int mv[FRAME_SAMPLES + 1];
    mv[FRAME_SAMPLES] = -1;
    mic_read_frame(mv, FRAME_SAMPLES);

    TEST_ASSERT_EQUAL(-1, mv[FRAME_SAMPLES]);
    TEST_ASSERT_EQUAL(1 + FRAME_SAMPLES, fake.adc_read);
}


void test_read_frame_reports_clipping_near_the_adc_rails(void)
{
    static const struct {
        int mv;
        bool clipped;
    } samples[] = {
        { MIC_CLIP_LOW_MV,      true },
        { MIC_CLIP_LOW_MV + 1,  false },
        { MIC_CLIP_HIGH_MV - 1, false },
        { MIC_CLIP_HIGH_MV,     true },
    };

    for (int i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        for (int j = 0; j < FRAME_SAMPLES - 1; j++)
            fake_adc_queue(MIC, 1650);
        fake_adc_queue(MIC, samples[i].mv);

        int mv[FRAME_SAMPLES];
        char message[32];
        snprintf(message, sizeof(message), "one sample at %d mV", samples[i].mv);
        TEST_ASSERT_EQUAL_MESSAGE(samples[i].clipped, mic_read_frame(mv, FRAME_SAMPLES), message);
    }
}


void test_read_frame_without_calibration_assumes_3100mv_full_scale(void)
{
    fake.adc_cali_in_efuse = false;
    boot();

    fake_adc_queue(MIC, 0);
    fake_adc_queue(MIC, 2048);
    fake_adc_queue(MIC, 4095);

    int mv[3];
    mic_read_frame(mv, 3);

    TEST_ASSERT_EQUAL(0, mv[0]);
    TEST_ASSERT_EQUAL(1550, mv[1]);
    TEST_ASSERT_EQUAL(3100, mv[2]);
}


// So ADC1 can take a oneshot battery reading in between.
void test_pause_stops_sampling_until_resume(void)
{
    mic_pause();
    TEST_ASSERT_FALSE(fake.adc_running);

    mic_resume();
    TEST_ASSERT_TRUE(fake.adc_running);

    for (int i = 0; i < FRAME_SAMPLES; i++)
        fake_adc_queue(MIC, 1650);
    int mv[FRAME_SAMPLES];
    mic_read_frame(mv, FRAME_SAMPLES);
    TEST_ASSERT_EACH_EQUAL_INT(1650, mv, FRAME_SAMPLES);
}


void test_sensitivity_starts_at_the_digipot_power_on_wiper(void)
{
    TEST_ASSERT_EQUAL(128, wiper());
}


void test_loud_lowers_sensitivity_by_a_quarter_once_held(void)
{
    frame_at(0, LOUD);
    frame_at(MIC_LOUD_HOLD_US - 1, LOUD);
    TEST_ASSERT_EQUAL(128, wiper());

    frame_at(MIC_LOUD_HOLD_US, LOUD);
    TEST_ASSERT_EQUAL(128 * 3 / 4, wiper());
}


void test_loud_keeps_lowering_sensitivity_while_it_lasts(void)
{
    frame_at(0, LOUD);
    frame_at(MIC_LOUD_HOLD_US, LOUD);
    frame_at(2 * MIC_LOUD_HOLD_US - 1, LOUD);
    TEST_ASSERT_EQUAL(96, wiper());

    frame_at(2 * MIC_LOUD_HOLD_US, LOUD);
    TEST_ASSERT_EQUAL(96 * 3 / 4, wiper());
}


void test_quiet_raises_sensitivity_by_an_eighth_once_held(void)
{
    frame_at(0, QUIET);
    frame_at(MIC_QUIET_HOLD_US - 1, QUIET);
    TEST_ASSERT_EQUAL(128, wiper());

    frame_at(MIC_QUIET_HOLD_US, QUIET);
    TEST_ASSERT_EQUAL(128 * 9 / 8, wiper());
}


void test_a_normal_frame_restarts_the_hold(void)
{
    frame_at(0, LOUD);
    frame_at(MIC_LOUD_HOLD_US / 2, NORMAL);
    frame_at(MIC_LOUD_HOLD_US / 2 + 1, LOUD);
    frame_at(MIC_LOUD_HOLD_US, LOUD);
    TEST_ASSERT_EQUAL(128, wiper());

    frame_at(MIC_LOUD_HOLD_US / 2 + 1 + MIC_LOUD_HOLD_US, LOUD);
    TEST_ASSERT_EQUAL(96, wiper());
}


// main.c reports both when the ADC clips but the bins the LEDs show are
// quiet, e.g. for a loud tone above the highest LED's frequency.
void test_loud_wins_over_quiet(void)
{
    frame_at(0, LOUD_AND_QUIET);
    frame_at(MIC_QUIET_HOLD_US, LOUD_AND_QUIET);

    TEST_ASSERT_EQUAL(96, wiper());
}


void test_sensitivity_bottoms_out_at_0_then_climbs_back(void)
{
    int64_t t = 0;
    for (int i = 0; i < 40; i++, t += MIC_LOUD_HOLD_US)
        frame_at(t, LOUD);
    TEST_ASSERT_EQUAL(0, wiper());

    // An eighth of 0 is 0, so this needs the minimum step of 1.
    frame_at(t, QUIET);
    frame_at(t + MIC_QUIET_HOLD_US, QUIET);
    TEST_ASSERT_EQUAL(1, wiper());
}


void test_sensitivity_tops_out_at_255(void)
{
    for (int i = 0; i < 20; i++)
        frame_at(i * MIC_QUIET_HOLD_US, QUIET);

    TEST_ASSERT_EQUAL(255, wiper());
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_samples_the_mic_at_10khz_keeping_only_the_newest_frame);
    RUN_TEST(test_read_frame_returns_each_sample_in_mv);
    RUN_TEST(test_read_frame_skips_results_from_other_channels);
    RUN_TEST(test_read_frame_keeps_reading_until_the_frame_is_full);
    RUN_TEST(test_read_frame_reads_no_further_than_the_frame);
    RUN_TEST(test_read_frame_reports_clipping_near_the_adc_rails);
    RUN_TEST(test_read_frame_without_calibration_assumes_3100mv_full_scale);
    RUN_TEST(test_pause_stops_sampling_until_resume);
    RUN_TEST(test_sensitivity_starts_at_the_digipot_power_on_wiper);
    RUN_TEST(test_loud_lowers_sensitivity_by_a_quarter_once_held);
    RUN_TEST(test_loud_keeps_lowering_sensitivity_while_it_lasts);
    RUN_TEST(test_quiet_raises_sensitivity_by_an_eighth_once_held);
    RUN_TEST(test_a_normal_frame_restarts_the_hold);
    RUN_TEST(test_loud_wins_over_quiet);
    RUN_TEST(test_sensitivity_bottoms_out_at_0_then_climbs_back);
    RUN_TEST(test_sensitivity_tops_out_at_255);
    return UNITY_END();
}
