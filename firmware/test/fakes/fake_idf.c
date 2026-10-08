#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/rmt_tx.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "led_strip_encoder.h"
#include "soc/soc_caps.h"
#include "unity.h"

#include "fake_idf.h"

fake_idf_t fake;

// Every handle the fakes return points here.  The firmware never looks inside.
static int handle;


void fake_idf_reset(void)
{
    memset(&fake, 0, sizeof(fake));

    // mic.c treats a time of 0 as "not yet", so start a second after boot.
    fake.now_us = 1000000;

    for (int i = 0; i < GPIO_NUM_MAX; i++)
        fake.gpio_level[i] = 1;

    fake.adc_read_chunk = FAKE_ADC_MAX_RESULTS;
    fake.adc_cali_in_efuse = true;
}


void fake_esp_log(const char *tag, const char *format, ...)
{
    int n = snprintf(fake.last_log, sizeof(fake.last_log), "%s: ", tag);
    if (n < 0 || n >= (int)sizeof(fake.last_log))
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(fake.last_log + n, sizeof(fake.last_log) - n, format, args);
    va_end(args);
}


void fake_adc_queue(adc_channel_t channel, int raw)
{
    TEST_ASSERT_LESS_THAN_MESSAGE(FAKE_ADC_MAX_RESULTS, fake.adc_queued, "Too many ADC results queued");
    TEST_ASSERT_TRUE_MESSAGE(raw >= 0 && raw <= 4095, "ADC readings are 12 bits");

    adc_digi_output_data_t *result = &fake.adc_results[fake.adc_queued++];
    result->val = 0;
    result->type2.data = raw;
    result->type2.channel = channel;
    result->type2.unit = 0;
}


void fake_adc_drop_frame(void)
{
    TEST_ASSERT_TRUE_MESSAGE(fake.adc_running, "The ADC isn't sampling");
    TEST_ASSERT_TRUE_MESSAGE(fake.adc_callbacks.on_pool_ovf != NULL, "No on_pool_ovf callback registered");

    adc_continuous_evt_data_t edata = { 0 };
    fake.adc_callbacks.on_pool_ovf((adc_continuous_handle_t)&handle, &edata, fake.adc_callbacks_user_data);
}


void fake_esp_error_check_failed(esp_err_t rc, const char *file, int line, const char *function, const char *expression)
{
    static char message[256];
    snprintf(message, sizeof(message), "ESP_ERROR_CHECK(%s) got 0x%x in %s() at %s:%d",
             expression, rc, function, file, line);
    TEST_FAIL_MESSAGE(message);
}


int64_t esp_timer_get_time(void)
{
    return fake.now_us;
}


// GPIO

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
{
    fake.gpio_config = *pGPIOConfig;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    TEST_ASSERT_TRUE_MESSAGE(gpio_num >= 0 && gpio_num < GPIO_NUM_MAX, "No such GPIO");
    return fake.gpio_level[gpio_num];
}


// SPI

esp_err_t spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config, spi_dma_chan_t dma_chan)
{
    fake.spi_bus = *bus_config;
    return ESP_OK;
}

esp_err_t spi_bus_add_device(spi_host_device_t host_id, const spi_device_interface_config_t *dev_config, spi_device_handle_t *handle_out)
{
    fake.spi_device = *dev_config;
    *handle_out = (spi_device_handle_t)&handle;
    return ESP_OK;
}

esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *trans_desc)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(device, "SPI device used before spi_bus_add_device()");
    fake.spi_sent = *trans_desc;
    fake.spi_transactions++;
    return ESP_OK;
}

esp_err_t spi_bus_remove_device(spi_device_handle_t device)
{
    return ESP_OK;
}

esp_err_t spi_bus_free(spi_host_device_t host_id)
{
    return ESP_OK;
}


// ADC

esp_err_t adc_continuous_new_handle(const adc_continuous_handle_cfg_t *hdl_config, adc_continuous_handle_t *ret_handle)
{
    fake.adc_handle_config = *hdl_config;
    *ret_handle = (adc_continuous_handle_t)&handle;
    return ESP_OK;
}

esp_err_t adc_continuous_config(adc_continuous_handle_t adc, const adc_continuous_config_t *config)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, config->pattern_num, "The fake ADC samples one channel");

    // Copy the pattern, which the caller may keep on its stack.
    fake.adc_config = *config;
    fake.adc_pattern = config->adc_pattern[0];
    fake.adc_config.adc_pattern = &fake.adc_pattern;
    return ESP_OK;
}

esp_err_t adc_continuous_register_event_callbacks(adc_continuous_handle_t adc, const adc_continuous_evt_cbs_t *cbs, void *user_data)
{
    // Like the real driver, only before sampling starts.
    if (fake.adc_running)
        return ESP_ERR_INVALID_STATE;

    fake.adc_callbacks = *cbs;
    fake.adc_callbacks_user_data = user_data;
    return ESP_OK;
}

esp_err_t adc_continuous_start(adc_continuous_handle_t adc)
{
    if (fake.adc_running)
        return ESP_ERR_INVALID_STATE;

    fake.adc_running = true;
    return ESP_OK;
}

esp_err_t adc_continuous_read(adc_continuous_handle_t adc, uint8_t *buf, uint32_t length_max, uint32_t *out_length, uint32_t timeout_ms)
{
    *out_length = 0;
    if (!fake.adc_running)
        return ESP_ERR_INVALID_STATE;

    fake.now_us += fake.adc_read_wait_us;
    int count = 0;
    while (fake.adc_read < fake.adc_queued
           && count < fake.adc_read_chunk
           && (count + 1) * SOC_ADC_DIGI_RESULT_BYTES <= length_max) {
        memcpy(&buf[count * SOC_ADC_DIGI_RESULT_BYTES], &fake.adc_results[fake.adc_read++], SOC_ADC_DIGI_RESULT_BYTES);
        count++;
    }

    *out_length = count * SOC_ADC_DIGI_RESULT_BYTES;
    return count ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t adc_continuous_stop(adc_continuous_handle_t adc)
{
    if (!fake.adc_running)
        return ESP_ERR_INVALID_STATE;

    fake.adc_running = false;
    return ESP_OK;
}

esp_err_t adc_continuous_flush_pool(adc_continuous_handle_t adc)
{
    // Like the real driver, only while stopped.  It drops the results the
    // ADC finished before then.
    if (fake.adc_running)
        return ESP_ERR_INVALID_STATE;

    fake.adc_read = fake.adc_queued;
    return ESP_OK;
}

esp_err_t adc_continuous_deinit(adc_continuous_handle_t adc)
{
    return ESP_OK;
}

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit)
{
    fake.adc_oneshot_unit = *init_config;
    *ret_unit = (adc_oneshot_unit_handle_t)&handle;
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t adc, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config)
{
    fake.adc_oneshot_channel = channel;
    fake.adc_oneshot_channel_config = *config;
    return ESP_OK;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t adc, adc_channel_t chan, int *out_raw)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(adc, "ADC read before adc_oneshot_new_unit()");
    TEST_ASSERT_TRUE_MESSAGE(chan >= 0 && chan < FAKE_ADC_CHANNELS, "No such ADC channel");
    TEST_ASSERT_TRUE_MESSAGE(fake.adc_oneshot_raw[chan] >= 0 && fake.adc_oneshot_raw[chan] <= 4095, "ADC readings are 12 bits");

    // The real driver can't take the ADC while it samples continuously.
    if (fake.adc_running)
        return ESP_ERR_TIMEOUT;

    *out_raw = fake.adc_oneshot_raw[chan];
    fake.adc_oneshot_reads++;
    return ESP_OK;
}

esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t adc)
{
    return ESP_OK;
}

esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config, adc_cali_handle_t *ret_handle)
{
    if (!fake.adc_cali_in_efuse)
        return ESP_ERR_NOT_SUPPORTED;

    *ret_handle = (adc_cali_handle_t)&handle;
    return ESP_OK;
}

esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t cali)
{
    return cali ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t cali, int raw, int *voltage)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(cali, "Calibration used without a calibration scheme");
    fake.adc_cali_conversions++;
    *voltage = raw;
    return ESP_OK;
}


// RMT, and the LED strip encoder from the ESP-IDF examples

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan)
{
    *ret_chan = (rmt_channel_handle_t)&handle;
    return ESP_OK;
}

esp_err_t rmt_new_led_strip_encoder(const led_strip_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder)
{
    *ret_encoder = (rmt_encoder_handle_t)&handle;
    return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel)
{
    return ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t encoder, const void *payload, size_t payload_bytes, const rmt_transmit_config_t *config)
{
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(FAKE_RMT_MAX_BYTES, payload_bytes, "Too many bytes for the fake LED strip");

    memcpy(fake.rmt_sent, payload, payload_bytes);
    fake.rmt_sent_bytes = payload_bytes;
    fake.rmt_transmits++;
    fake.rmt_busy = true;
    return ESP_OK;
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t tx_channel, int timeout_ms)
{
    fake.rmt_busy = false;
    return ESP_OK;
}


// FreeRTOS.  Tasks are never run.

BaseType_t xTaskCreate(TaskFunction_t task_code, const char *name, uint32_t stack_depth, void *parameters, UBaseType_t priority, TaskHandle_t *created_task)
{
    if (created_task)
        *created_task = &handle;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
}

void vTaskDelay(TickType_t ticks)
{
}
