#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "mic.h"
#include "config.h"

const static char *TAG = "mic";

static bool mic_calibrated = false;
adc_oneshot_unit_handle_t mic_handle;
adc_cali_handle_t mic_cali_channel_handle = NULL;


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


void mic_stop(void) {
    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(mic_handle));
    if (mic_calibrated) {
        mic_calibration_deinit(mic_cali_channel_handle);
    }
}