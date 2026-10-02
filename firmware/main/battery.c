#include <math.h>
#include <stddef.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery.h"
#include "config.h"

const static char *TAG = "battery";

// R12 and R13 (100k each) halve the battery voltage at GPIO 0, so a full 4.2 V
// battery reads 2.1 V, within the ADC's range at 12 dB attenuation.  On v1.1
// they're wired straight to the battery.  On v1.2 they're after the power
// switch, so while USB is plugged in they read the USB supply (about 4.6 V)
// instead, which is never low.
#define BATTERY_DIVIDER         2

// The LEDs and the ESP32-C3 run from a 3.3 V regulator, which starts to drop
// out once the battery is under about 3.4 V, so warn a little before then.
// The battery stays low until it's back over BATTERY_OK_MV, so the warning
// doesn't flicker while the voltage wanders around BATTERY_LOW_MV.
#define BATTERY_LOW_MV          3600
#define BATTERY_OK_MV           3700

// The LEDs' current makes the battery voltage dip with the music, so readings
// are smoothed.  The smoothed voltage moves about two thirds (1 - 1/e) of the
// way to a reading taken this long after the last one.  Measured in time, not
// readings, so it doesn't depend on how often main.c checks.
#define BATTERY_SMOOTHING_US    30000000

// The ADC is noisy, so each reading averages this many samples.
#define BATTERY_SAMPLES         16

static adc_oneshot_unit_handle_t battery_handle = NULL;
static adc_cali_handle_t battery_cali_handle = NULL;
static float battery_mv = 0;
static int64_t battery_read_time = 0;
static bool battery_low = false;


void battery_init(void) {
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = CONFIG_BATTERY_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &battery_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = CONFIG_BATTERY_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(battery_handle, CONFIG_BATTERY_CHANNEL, &channel_config));

    // The ESP32-C3 only supports curve fitting.
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = CONFIG_BATTERY_UNIT,
        .chan = CONFIG_BATTERY_CHANNEL,
        .atten = CONFIG_BATTERY_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &battery_cali_handle) != ESP_OK) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
        battery_cali_handle = NULL;
    }
}


static int battery_raw_to_voltage(int adc_raw) {
    int voltage;

    if (battery_cali_handle) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(battery_cali_handle, adc_raw, &voltage));
    } else {
        voltage = (adc_raw * 3100) / 4095;
    }

    return voltage;
}


// Returns the battery voltage in mV.
static int battery_read_mv(void) {
    int total = 0;

    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(battery_handle, CONFIG_BATTERY_CHANNEL, &raw));
        total += battery_raw_to_voltage(raw);
    }

    return total * BATTERY_DIVIDER / BATTERY_SAMPLES;
}


// Reads the battery and returns whether it's low.  ADC1 can't take a oneshot
// reading while it samples the mic continuously, so call this before
// mic_init(), or between mic_pause() and mic_resume().
bool battery_check(void) {
    int mv = battery_read_mv();
    int64_t now = esp_timer_get_time();
    bool was_low = battery_low;

    // The first reading, at power-on before the LEDs draw any current, is
    // taken as is.
    if (battery_read_time == 0)
        battery_mv = mv;
    else
        battery_mv += (mv - battery_mv) * (1 - expf(-(float)(now - battery_read_time) / BATTERY_SMOOTHING_US));
    battery_read_time = now;

    if (battery_mv < BATTERY_LOW_MV)
        battery_low = true;
    else if (battery_mv > BATTERY_OK_MV)
        battery_low = false;

    ESP_LOGI(TAG, "%d mV, smoothed %d mV", mv, (int)battery_mv);
    if (battery_low && !was_low)
        ESP_LOGW(TAG, "low, under %d mV", BATTERY_LOW_MV);
    else if (!battery_low && was_low)
        ESP_LOGI(TAG, "no longer low, over %d mV", BATTERY_OK_MV);

    return battery_low;
}


void battery_stop(void) {
    if (battery_cali_handle) {
        ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(battery_cali_handle));
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(battery_handle));
}
