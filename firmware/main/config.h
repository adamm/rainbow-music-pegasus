#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#define CONFIG_MIC_UNIT                ADC_UNIT_1
#define CONFIG_MIC_CHANNEL             ADC_CHANNEL_4   // GPIO 4
#define CONFIG_BATTERY_UNIT            ADC_UNIT_1
#define CONFIG_BATTERY_CHANNEL         ADC_CHANNEL_0   // GPIO 0
#else
#error "Only ESP32C3 is tested/supported. The original ESP32 won't work due to how audio is read. YMMV for other ESP32 models."
#endif
#define CONFIG_MIC_ATTEN               ADC_ATTEN_DB_12
#define CONFIG_BATTERY_ATTEN           ADC_ATTEN_DB_12

// The LEDs show 0 Hz up to this frequency whatever their count, shared equally
// between the pairs, so more LEDs show it in finer steps.  config_init() picks
// the sample rate to fit.  2344 Hz keeps a 10-LED board sampling at about
// 10 kHz, as before.  Much above 4 kHz, the mic's anti-aliasing filter (R17 and
// C41, -3 dB at 4.8 kHz) dims the highest LEDs.
#define CONFIG_LEDS_TOP_FREQ_HZ        2344

// How leds_display() lays the FFT bins out on the LEDs' colour channels.  Each
// pair of LEDs is the same place on the right (even index) and left wing.
typedef enum {
    // Three bins per pair, lowest first: green, red, blue on the right LED,
    // and the same bins on the left with the channels rotated.
    CONFIG_PATTERN_A,
    // One bin per channel, six per pair, lowest first: the right then left
    // LED's red, then their green, then their blue.
    CONFIG_PATTERN_B,
    // One bin per channel: the lowest third light every LED's red in strip
    // order, the middle third their green, and the top third their blue.
    CONFIG_PATTERN_C,
} config_pattern_t;

// Patterns B and C show twice as many bins as A, so they use FFT sizes twice
// as large to keep the same sample rates and frequency range.  Their bins are
// half as wide, and their frames twice as long, up to 30.7 ms with 24 LEDs.
#define CONFIG_LEDS_PATTERN            CONFIG_PATTERN_C

#define CONFIG_GPIO_RGB_DATA           3

#define CONFIG_GPIO_TOTAL_LEDS_ADD_2   7
#define CONFIG_GPIO_TOTAL_LEDS_ADD_4   6
#define CONFIG_GPIO_TOTAL_LEDS_ADD_8   5

#define CONFIG_MIN_LEDS               10
#define CONFIG_MAX_LEDS               24

#define CONFIG_GPIO_DIGIPOT_CS        18
#define CONFIG_GPIO_DIGIPOT_MOSI      10
#define CONFIG_GPIO_DIGIPOT_CLK       19

extern uint8_t _config_total_leds;
extern config_pattern_t _config_pattern;
extern uint16_t _config_total_samples;
extern uint32_t _config_sample_freq_hz;

void config_init();
uint16_t config_displayed_bins(void);
