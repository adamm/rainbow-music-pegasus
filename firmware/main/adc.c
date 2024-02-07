#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "adc.h"
#include "config.h"

const static char *TAG = "adc";

bool adc_calibrated = false;
adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t adc_cali_channel_handle = NULL;


bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!adc_calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            adc_calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!adc_calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            adc_calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !adc_calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return adc_calibrated;
}


void adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}


void adc_init(void) {
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = CONFIG_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc_handle));

    adc_oneshot_chan_cfg_t adc_channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = CONFIG_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, CONFIG_ADC_CHANNEL, &adc_channel_config));

    adc_calibration_init(CONFIG_ADC_UNIT, CONFIG_ADC_CHANNEL, CONFIG_ADC_ATTEN, &adc_cali_channel_handle);
}


int adc_read(void) {
    int adc_raw;
    int voltage;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, CONFIG_ADC_CHANNEL, &adc_raw));
    // ESP_LOGI(TAG, "Read ADC Raw Data: %d", adc_raw);

    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_channel_handle, adc_raw, &voltage));
    // ESP_LOGI(TAG, "Calculate ADC Voltage: %d mV", voltage);

    return voltage;
}


void adc_stop(void) {
    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    if (adc_calibrated) {
        adc_calibration_deinit(adc_cali_channel_handle);
    }
}