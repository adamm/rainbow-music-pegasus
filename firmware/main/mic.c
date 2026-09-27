#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "mic.h"
#include "config.h"
#include "digipot.h"

const static char *TAG = "mic";

// A frame is clipping if any sample gets this close to the ADC rails.  With
// 12 dB attenuation the ESP32-C3 ADC is only specified up to ~2500 mV, so the
// positive half-wave clips first.
#define MIC_CLIP_LOW_MV         100
#define MIC_CLIP_HIGH_MV        2400
// A frame is too quiet if its peak-to-peak is buried in the ADC noise.
#define MIC_QUIET_P2P_MV        50
// How long a frame condition must persist before stepping the sensitivity.
// Measured in time, not frames, since the frame length depends on the FFT size.
#define MIC_LOUD_HOLD_US        50000
#define MIC_QUIET_HOLD_US       200000

// Sensitivity is the digipot wiper code.  The digipot's B-W resistance is the
// feedback resistor of U2B, so the preamp gain scales linearly with it.
// Code 00h puts the wiper at terminal B, so a higher code means higher gain.
#define MIC_SENSITIVITY_INIT    128  // same as the MCP41050 power-on wiper
#define MIC_SENSITIVITY_MAX     255

static bool mic_calibrated = false;
adc_oneshot_unit_handle_t mic_handle;
adc_cali_handle_t mic_cali_channel_handle = NULL;
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


void mic_init(void) {
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = CONFIG_MIC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &mic_handle));

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = CONFIG_MIC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(mic_handle, CONFIG_MIC_CHANNEL, &adc_channel_config));

    mic_calibration_init(CONFIG_MIC_UNIT, CONFIG_MIC_CHANNEL, CONFIG_MIC_ATTEN, &mic_cali_channel_handle);

    // Requires digipot_init() to have been called first.
    mic_sensitivity_set(MIC_SENSITIVITY_INIT);
}


int mic_read(void) {
    int adc_raw;
    int voltage;

    ESP_ERROR_CHECK(adc_oneshot_read(mic_handle, CONFIG_MIC_CHANNEL, &adc_raw));
    // ESP_LOGI(TAG, "Read ADC Raw Data: %d", adc_raw);

    if (mic_cali_channel_handle) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(mic_cali_channel_handle, adc_raw, &voltage));
    } else {
        voltage = (adc_raw * 3100) / 4095;
    }
    // ESP_LOGI(TAG, "Calculate ADC Voltage: %d mV", voltage);

    return voltage;
}


// Called once per frame with the lowest and highest voltage read in it.
// Progressively lowers the preamp gain while frames keep clipping, and raises
// it while frames stay too quiet to be useful.  Clipping is stepped down faster
// (~2.5 dB) than quiet is stepped up (~1 dB).  Steps are proportional to the
// current sensitivity so each one is roughly the same number of dB.
void mic_sensitivity_update(int min_mv, int max_mv) {
    static int64_t loud_since = 0;
    static int64_t quiet_since = 0;
    int64_t now = esp_timer_get_time();
    bool loud = (min_mv <= MIC_CLIP_LOW_MV || max_mv >= MIC_CLIP_HIGH_MV);
    bool quiet = (max_mv - min_mv < MIC_QUIET_P2P_MV);
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


void mic_stop(void) {
    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(mic_handle));
    if (mic_calibrated) {
        mic_calibration_deinit(mic_cali_channel_handle);
    }
}