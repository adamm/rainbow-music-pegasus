#include <assert.h>
#include <stdlib.h>

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"

#include "mic.h"
#include "config.h"
#include "digipot.h"

const static char *TAG = "mic";

// A frame is clipping if any sample gets this close to the ADC rails.  With
// 12 dB attenuation the ESP32-C3 ADC is only specified up to ~2500 mV, so the
// positive half-wave clips first.
#define MIC_CLIP_LOW_MV         100
#define MIC_CLIP_HIGH_MV        2400
// How long a frame condition must persist before stepping the sensitivity.
// Measured in time, not frames, since the frame length depends on the FFT size.
#define MIC_LOUD_HOLD_US        50000
#define MIC_QUIET_HOLD_US       200000

// Sensitivity is the digipot wiper code.  The digipot's B-W resistance is the
// feedback resistor of U2B, so the preamp gain scales linearly with it.
// Code 00h puts the wiper at terminal B, so a higher code means higher gain.
#define MIC_SENSITIVITY_INIT    128  // same as the MCP41050 power-on wiper
#define MIC_SENSITIVITY_MAX     255

// A frame lasts LEDs * 1.5 / CONFIG_LEDS_TOP_FREQ_HZ, at most 15.4 ms at the
// default top frequency, so waiting this long means the ADC has stopped.
#define MIC_READ_TIMEOUT_MS     1000

static bool mic_calibrated = false;
adc_continuous_handle_t mic_handle;
adc_cali_handle_t mic_cali_channel_handle = NULL;
static uint8_t* mic_frame = NULL;
static int mic_sensitivity = 0;


static void mic_sensitivity_set(int sensitivity) {
    if (sensitivity < 0)                   sensitivity = 0;
    if (sensitivity > MIC_SENSITIVITY_MAX) sensitivity = MIC_SENSITIVITY_MAX;

    mic_sensitivity = sensitivity;
    digipot_set_value(sensitivity);
}


bool mic_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!mic_calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            mic_calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!mic_calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            mic_calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !mic_calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return mic_calibrated;
}


void mic_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}


// The ADC samples the mic continuously by DMA, so the sample timing is set by
// hardware and can't be disturbed by other tasks or interrupts.  Requires
// config_init() to have been called first to size the frame and set the rate.
void mic_init(void) {
    uint32_t frame_size = _config_total_samples * SOC_ADC_DIGI_RESULT_BYTES;

    // The driver hands over one FFT frame at a time and keeps only the newest
    // one.  If the main loop falls behind, older frames are dropped, so the
    // LEDs never lag the sound by more than a frame.
    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = frame_size,
        .conv_frame_size = frame_size,
        .flags.flush_pool = 1,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_config, &mic_handle));

    adc_digi_pattern_config_t pattern = {
        .atten = CONFIG_MIC_ATTEN,
        .channel = CONFIG_MIC_CHANNEL,
        .unit = CONFIG_MIC_UNIT,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };
    adc_continuous_config_t adc_config = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = _config_sample_freq_hz,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_ERROR_CHECK(adc_continuous_config(mic_handle, &adc_config));

    mic_frame = malloc(frame_size);
    assert(mic_frame);

    mic_calibration_init(CONFIG_MIC_UNIT, CONFIG_MIC_CHANNEL, CONFIG_MIC_ATTEN, &mic_cali_channel_handle);

    // Requires digipot_init() to have been called first.
    mic_sensitivity_set(MIC_SENSITIVITY_INIT);

    ESP_ERROR_CHECK(adc_continuous_start(mic_handle));
}


static int mic_raw_to_voltage(int adc_raw) {
    int voltage;

    if (mic_cali_channel_handle) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(mic_cali_channel_handle, adc_raw, &voltage));
    } else {
        voltage = (adc_raw * 3100) / 4095;
    }

    return voltage;
}


// Blocks until total_samples consecutive samples are ready, and returns them
// in mV.  Returns true if any sample got close enough to the ADC rails to have
// clipped.  total_samples must not exceed the frame size set by mic_init().
bool mic_read_frame(int* voltages, int total_samples) {
    bool clipped = false;
    int n = 0;

    while (n < total_samples) {
        uint32_t length = 0;
        uint32_t wanted = (total_samples - n) * SOC_ADC_DIGI_RESULT_BYTES;

        // The driver can return part of a frame, so keep reading until it's full.
        ESP_ERROR_CHECK(adc_continuous_read(mic_handle, mic_frame, wanted, &length, MIC_READ_TIMEOUT_MS));

        for (uint32_t i = 0; i < length; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t* result = (adc_digi_output_data_t*)&mic_frame[i];

            // Skip the occasional invalid result, which reports a bogus channel.
            if (result->type2.channel != CONFIG_MIC_CHANNEL)
                continue;
            int voltage = mic_raw_to_voltage(result->type2.data);
            if (voltage <= MIC_CLIP_LOW_MV || voltage >= MIC_CLIP_HIGH_MV)
                clipped = true;
            voltages[n++] = voltage;
        }
    }

    return clipped;
}


// Called once per frame with whether that frame was too loud or too quiet.
// Progressively lowers the preamp gain while frames keep being too loud, and
// raises it while they stay too quiet.  Loud is stepped down faster (~2.5 dB)
// than quiet is stepped up (~1 dB).  Steps are proportional to the current
// sensitivity so each one is roughly the same number of dB.
void mic_sensitivity_update(bool loud, bool quiet) {
    static int64_t loud_since = 0;
    static int64_t quiet_since = 0;
    int64_t now = esp_timer_get_time();
    int old = mic_sensitivity;

    if (!loud)
        loud_since = 0;
    else if (loud_since == 0)
        loud_since = now;

    if (!quiet)
        quiet_since = 0;
    else if (quiet_since == 0)
        quiet_since = now;

    if (loud_since && now - loud_since >= MIC_LOUD_HOLD_US) {
        int step = mic_sensitivity / 4;
        mic_sensitivity_set(mic_sensitivity - (step > 1 ? step : 1));
        loud_since = now;
        if (mic_sensitivity != old)
            ESP_LOGI(TAG, "too loud, sensitivity %d -> %d", old, mic_sensitivity);
    }
    else if (quiet_since && now - quiet_since >= MIC_QUIET_HOLD_US) {
        int step = mic_sensitivity / 8;
        mic_sensitivity_set(mic_sensitivity + (step > 1 ? step : 1));
        quiet_since = now;
        if (mic_sensitivity != old)
            ESP_LOGI(TAG, "too quiet, sensitivity %d -> %d", old, mic_sensitivity);
    }
}


// ADC1 can sample continuously or take oneshot readings, but not both at once,
// so pause the mic while another of its channels, such as the battery, is read.
void mic_pause(void) {
    ESP_ERROR_CHECK(adc_continuous_stop(mic_handle));
}


void mic_resume(void) {
    ESP_ERROR_CHECK(adc_continuous_start(mic_handle));
}


void mic_stop(void) {
    //Tear Down
    ESP_ERROR_CHECK(adc_continuous_stop(mic_handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(mic_handle));
    free(mic_frame);
    mic_frame = NULL;
    if (mic_calibrated) {
        mic_calibration_deinit(mic_cali_channel_handle);
    }
}