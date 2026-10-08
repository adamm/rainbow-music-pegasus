/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_dsp.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"


#include "battery.h"
#include "config.h"
#include "digipot.h"
#include "fft.h"
#include "leds.h"
#include "mic.h"

const static char *TAG = "main";



#define N_SAMPLES _config_total_samples
// Each frame overlaps the one before by half, so the LEDs update every half
// frame, every 15.4 ms with 24 LEDs in pattern C.  Updating only once a frame
// sampled a held note's level too slowly to follow quick changes in it, such
// as close partials beating, and turned them into a slow flutter.
#define N_HOP (N_SAMPLES / 2)
// leds_display() only shows the lowest bins, three per pair of LEDs in pattern
// A and three per LED in the others.
#define N_DISPLAYED_BINS config_displayed_bins()

// Each bin is converted to the amplitude, in mV at the ADC, of a sine wave that
// would produce it, so the LEDs behave the same whatever the frame size.  Bins
// under the noise floor stay dark so amp and ADC hiss doesn't flicker the LEDs,
// and bins at full scale light their LED at full brightness.  Full scale is well
// under the ADC's ~750 mV of headroom, leaving room for peaks across many bins.
#define FFT_NOISE_FLOOR_MV 8.5f
#define FFT_FULL_SCALE_MV  150.0f

#define DB_TO_RATIO(db) powf(10, (db) / 20)

// Each bin's floor is the level of the room's background sound in it, such as a
// crowd talking, and the bin lights only once it's FLOOR_MARGIN_DB over that.
// The floor rises slowly while the bin is louder than it and falls three times
// as fast while it's quieter, so it settles where the bin is quieter than it a
// quarter of the time.  A crowd never goes quiet, so the floor sits on it, but
// beats are loud for much less than three quarters of the time and stand out.
// Anything held steady is learned too, so a note held 20 dB over the threshold
// fades out in about 20 seconds.  Noise in a bin jumps around from frame to
// frame, and reaches the margin over its quietest quarter in only 1 to 2% of
// frames.
#define FLOOR_RISE_DB_PER_S  1.0f
#define FLOOR_FALL_DB_PER_S  3.0f
#define FLOOR_MARGIN_DB      12.0f
// Below this the threshold would be under FFT_NOISE_FLOOR_MV anyway, so the
// floor stops here, ready to learn a crowd from where it starts to matter.
#define FLOOR_LOWEST_MV      (FFT_NOISE_FLOOR_MV / DB_TO_RATIO(FLOOR_MARGIN_DB))
// While the mic settles at power-on, the floor learns the room this many times
// faster, so it can climb 45 dB in the 3 seconds.
#define FLOOR_SETTLE_SPEEDUP 15.0f
// How often to log how many of the shown bins' floors went up or down, to watch
// them learn a room.  A floor counts as moved once it's changed by more than
// FLOOR_LOG_MOVED_DB since the last log, so a settled floor's jitter doesn't.
#define FLOOR_LOG_US         1000000
#define FLOOR_LOG_MOVED_DB   0.5f

// Once the sound in a bin drops, its LED fades to about a third (1/e) of its
// brightness in this time.  Fading by a fraction rather than a fixed step keeps
// dim LEDs from blinking out, so quiet music doesn't flicker.  Rises show
// immediately.
#define LED_DECAY_US 150000

// How often to check the battery during the light show.  Each check delays the
// next frame by up to a frame, which is too brief and rare to notice.
#define BATTERY_CHECK_US 10000000

// How often to log how long the light show takes to process a frame, and how
// many hops of new samples the mic dropped because the frame before was still
// being processed.  The loop keeps up while processing a frame takes less time
// than recording a hop.
#define FRAME_LOG_US         1000000

// Each shown bin's floor at the last floor log, and when that was, 0 for never.
static float floor_logged[CONFIG_MAX_LEDS * 3];
static int64_t floor_logged_us = 0;

// Whether the samples kept from the last frame are out of date, so the next
// frame must be read whole: at power-on, and after the mic pauses.  And
// mic_frames_dropped() after the last frame was read, as a dropped hop leaves
// a gap after them too.
static bool frame_stale = true;
static uint32_t frame_dropped = 0;

// The time spent on each step of the frames since the last frame log, in µs.
static struct {
    int frames;
    int64_t read_us;       // converting samples to mV, not waiting for them
    int64_t window_us;     // removing DC and applying the window
    int64_t fft_us;
    int64_t magnitude_us;
    int64_t busy_us;       // the whole frame, except waiting for samples
    int64_t max_busy_us;
    int64_t frame_end_us;  // when the last frame ended
    int64_t waited_us;     // mic_time_waited_us() then
    uint32_t dropped;      // mic_frames_dropped() at the last log
    int64_t logged_us;
} frame_stats;


// Add the time since *since to *total_us, and move *since on to now.
static void time_step(int64_t* total_us, int64_t* since)
{
    int64_t now = esp_timer_get_time();
    *total_us += now - *since;
    *since = now;
}


// Read a hop of samples from the mic into the end of the frame in voltages,
// after the newest samples of the last frame, and put the shown bins' levels,
// in mV, in vReal.  The bins past them are left as they were, as nothing uses
// them.
// Then let the mic adjust its sensitivity to fit the LEDs: too loud if the ADC
// clipped or the brightest LED would be at full brightness, too quiet if the
// loudest bin is under a quarter of that.  The 12 dB gap between the two is
// several sensitivity steps wide, so the gain settles instead of hunting.
// Returns how much the mic's gain changed, as a ratio.
static float read_spectrum(int* voltages, float* vReal)
{
    float peak = 0;
    int64_t step_start = esp_timer_get_time();
    int64_t waited = mic_time_waited_us();
    bool clipped = false;
    if (frame_stale || mic_frames_dropped() != frame_dropped) {
        for (int start = 0; start < N_SAMPLES; start += N_HOP)
            clipped |= mic_read_frame(voltages + start, N_HOP);
        frame_stale = false;
    } else {
        memmove(voltages, voltages + N_HOP, (N_SAMPLES - N_HOP) * sizeof(int));
        clipped = mic_read_frame(voltages + N_SAMPLES - N_HOP, N_HOP);
    }
    frame_dropped = mic_frames_dropped();
    frame_stats.read_us -= mic_time_waited_us() - waited;
    time_step(&frame_stats.read_us, &step_start);

    fft_load(voltages);
    time_step(&frame_stats.window_us, &step_start);
    fft_compute();
    time_step(&frame_stats.fft_us, &step_start);
    int bins = N_DISPLAYED_BINS;
    fft_magnitudes(vReal, bins);
    time_step(&frame_stats.magnitude_us, &step_start);

    for (int i = 0; i < bins; i++) {
        if (vReal[i] > peak)
            peak = vReal[i];
    }

    return mic_sensitivity_update(clipped || peak >= FFT_FULL_SCALE_MV, peak < FFT_FULL_SCALE_MV / 4);
}


// Learn each shown bin's floor in vFloor from this frame's bins in vReal, in
// mV, elapsed_us after the last frame, and speedup times faster than normal.
// Then follow the mic's gain, which changes by gain_change within a frame or
// so.
static void track_floor(const float* vReal, float* vFloor, float gain_change, int64_t elapsed_us, float speedup)
{
    float seconds = speedup * elapsed_us / 1e6f;
    float rise = DB_TO_RATIO(FLOOR_RISE_DB_PER_S * seconds) * gain_change;
    float fall = DB_TO_RATIO(-FLOOR_FALL_DB_PER_S * seconds) * gain_change;
    int bins = N_DISPLAYED_BINS;

    for (int i = 0; i < bins; i++) {
        vFloor[i] *= vReal[i] > vFloor[i] ? rise : fall;
        if (vFloor[i] < FLOOR_LOWEST_MV)
            vFloor[i] = FLOOR_LOWEST_MV;
    }
}


// Every FLOOR_LOG_US, log how many of the shown bins' floors went up or down.
// Leave out the mic's gain changes, gain_change since the last frame, which
// mic.c logs itself.
static void log_floor(const float* vFloor, float gain_change, int64_t now)
{
    for (int i = 0; i < N_DISPLAYED_BINS; i++)
        floor_logged[i] *= gain_change;

    if (floor_logged_us != 0 && now - floor_logged_us < FLOOR_LOG_US)
        return;

    if (floor_logged_us != 0) {
        float moved = DB_TO_RATIO(FLOOR_LOG_MOVED_DB);
        int up = 0, down = 0;

        for (int i = 0; i < N_DISPLAYED_BINS; i++) {
            if (vFloor[i] > floor_logged[i] * moved)
                up++;
            else if (vFloor[i] < floor_logged[i] / moved)
                down++;
        }
        ESP_LOGI(TAG, "adjusted the floor up on %d bins and down on %d, of %d", up, down, N_DISPLAYED_BINS);
    }

    memcpy(floor_logged, vFloor, N_DISPLAYED_BINS * sizeof(float));
    floor_logged_us = now;
}


// Start timing the light show's frames, from `now`.
static void start_frame_log(int64_t now)
{
    memset(&frame_stats, 0, sizeof(frame_stats));
    frame_stats.frame_end_us = now;
    frame_stats.waited_us = mic_time_waited_us();
    frame_stats.dropped = mic_frames_dropped();
    frame_stats.logged_us = now;
}


// The average of total_us over the frames since the last frame log, in ms.
static float ms_per_frame(int64_t total_us)
{
    return total_us / 1000.0f / frame_stats.frames;
}


// Count a frame of the light show that ended at `now`.  Every FRAME_LOG_US,
// log how long frames took to process, step by step, and how many hops the
// mic dropped.  The rest is the floor, the colours, sending them to the LEDs, the
// battery check and logging.
static void log_frames(int64_t now)
{
    int64_t waited = mic_time_waited_us();
    int64_t busy = now - frame_stats.frame_end_us - (waited - frame_stats.waited_us);

    frame_stats.frames++;
    frame_stats.busy_us += busy;
    if (busy > frame_stats.max_busy_us)
        frame_stats.max_busy_us = busy;
    frame_stats.frame_end_us = now;
    frame_stats.waited_us = waited;

    if (now - frame_stats.logged_us < FRAME_LOG_US)
        return;

    uint32_t dropped = mic_frames_dropped();
    int64_t steps_us = frame_stats.read_us + frame_stats.window_us + frame_stats.fft_us + frame_stats.magnitude_us;
    ESP_LOGI(TAG, "%d frames of %.1f ms, %.1f ms apart, %d dropped, %.1f ms busy each (max %.1f): "
             "read %.1f, window %.1f, FFT %.1f, magnitude %.1f, rest %.1f",
             frame_stats.frames, N_SAMPLES * 1000.0f / _config_sample_freq_hz,
             N_HOP * 1000.0f / _config_sample_freq_hz, (int)(dropped - frame_stats.dropped),
             ms_per_frame(frame_stats.busy_us), frame_stats.max_busy_us / 1000.0f,
             ms_per_frame(frame_stats.read_us), ms_per_frame(frame_stats.window_us),
             ms_per_frame(frame_stats.fft_us), ms_per_frame(frame_stats.magnitude_us),
             ms_per_frame(frame_stats.busy_us - steps_us));

    start_frame_log(now);
}


// Replace each shown bin in vReal with its LED brightness, and set colours
// from it.  A bin lights once it's FLOOR_MARGIN_DB over its floor in vFloor,
// and never under FFT_NOISE_FLOOR_MV.  vDecay holds each LED's brightness from
// the last frame, elapsed_us ago: louder bins show immediately, and quieter
// ones fade from there.
static void spectrum_to_colours(float* vReal, const float* vFloor, float* vDecay, uint8_t* colours, int64_t elapsed_us)
{
    float decay = expf(-(float)elapsed_us / LED_DECAY_US);
    float margin = DB_TO_RATIO(FLOOR_MARGIN_DB);
    int bins = N_DISPLAYED_BINS;

    for (int i = 0; i < bins; i++) {
        // Scale each bin from its threshold up to full scale to an LED
        // brightness from 0 to 250, so music over a noisy room can still reach
        // full brightness.
        float threshold = fmaxf(FFT_NOISE_FLOOR_MV, margin * vFloor[i]);
        if (vReal[i] <= threshold)
            vReal[i] = 0;
        else if (vReal[i] >= FFT_FULL_SCALE_MV)
            vReal[i] = 250;
        else
            vReal[i] = (vReal[i] - threshold) * 250 / (FFT_FULL_SCALE_MV - threshold);

        if (vReal[i] > vDecay[i])
            vDecay[i] = vReal[i];
        else
            vDecay[i] *= decay;

        colours[i] = (uint8_t)vDecay[i];
    }
}


// Read the battery, and have the LEDs warn if it's low.  ADC1 can't take a
// battery reading while it samples the mic continuously, so pause the mic for
// it.  The samples kept from the last frame end before the pause, so read the
// next frame whole.
static void read_battery(void)
{
    mic_pause();
    leds_show_low_battery(battery_check());
    mic_resume();
    frame_stale = true;
}


void app_main(void)
{
    config_init();
    int* voltages = malloc(sizeof(int) * _config_total_samples);
    float* vReal = malloc(sizeof(float) * _config_total_samples);
    float* vDecay = malloc(sizeof(float) * _config_total_samples);
    float* vFloor = malloc(sizeof(float) * _config_total_samples);
    uint8_t* colours = malloc(sizeof(uint8_t) * _config_total_samples);

    bzero(voltages, sizeof(int) * _config_total_samples);
    bzero(vReal, sizeof(float) * _config_total_samples);
    bzero(vDecay, sizeof(float) * _config_total_samples);
    bzero(colours, sizeof(uint8_t) * _config_total_samples);
    for (int i = 0; i < _config_total_samples; i++)
        vFloor[i] = FLOOR_LOWEST_MV;

    digipot_init();
    battery_init();
    // The mic isn't sampling yet, so ADC1 is free for the first battery reading.
    leds_show_low_battery(battery_check());
    mic_init(N_HOP);
    leds_init();
    leds_scanning_start();
    fft_init(N_SAMPLES);

    uint64_t start_settle_time = esp_timer_get_time();
    int64_t last_frame_time = start_settle_time;

    // Give the mic a few seconds to settle its sensitivity, and the floor to
    // learn the room, before the light show starts.
    ESP_LOGI(TAG, "Settling mic sensitivity...");
    while (esp_timer_get_time()-start_settle_time < 3000000) {
        float gain_change = read_spectrum(voltages, vReal);
        int64_t now = esp_timer_get_time();
        track_floor(vReal, vFloor, gain_change, now - last_frame_time, FLOOR_SETTLE_SPEEDUP);
        log_floor(vFloor, gain_change, now);
        last_frame_time = now;
    }
    ESP_LOGI(TAG, "Finished settling mic sensitivity...");
    leds_scanning_stop();

    // Begin light show
    int64_t last_battery_time = esp_timer_get_time();
    start_frame_log(last_battery_time);
    while (1) {
        float gain_change = read_spectrum(voltages, vReal);

        // Base the floor and the fade on the time since the last frame, so
        // their speed doesn't depend on the frame size or on frames the driver
        // dropped.
        int64_t now = esp_timer_get_time();
        track_floor(vReal, vFloor, gain_change, now - last_frame_time, 1);
        log_floor(vFloor, gain_change, now);
        spectrum_to_colours(vReal, vFloor, vDecay, colours, now - last_frame_time);
        last_frame_time = now;

        // printf("R: ");
        // for (int i = 0; i < 16; i++) {
        //     printf("%0.1f ", vReal[i]);
        // }
        // printf("\nD: ");
        // for (int i = 0; i < 16; i++) {
        //     printf("%0.1f ", vDecay[i]);
        // }
        // printf("\n");
        // ESP_LOGI(TAG, "fft");
        // dsps_view(vReal, N_SAMPLES, 64, 10, 0, 255, '-');

        leds_display(colours, N_DISPLAYED_BINS);

        if (now - last_battery_time >= BATTERY_CHECK_US) {
            read_battery();
            last_battery_time = now;
        }

        log_frames(esp_timer_get_time());
    }

    mic_stop();
    battery_stop();
    digipot_stop();
}

