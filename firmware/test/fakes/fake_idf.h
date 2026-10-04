// Fakes for the parts of ESP-IDF the firmware uses, so its modules can be
// built and tested on the host.  A test sets up `fake` with what the hardware
// should report, runs the firmware, then checks what the firmware did with it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"

#define FAKE_ADC_MAX_RESULTS    1024
#define FAKE_ADC_CHANNELS       8
#define FAKE_RMT_MAX_BYTES      256
#define FAKE_LOG_MAX_BYTES      256

typedef struct {
    // What esp_timer_get_time() returns.
    int64_t now_us;

    // The last line logged, as "TAG: message", or "" if nothing has been.
    char last_log[FAKE_LOG_MAX_BYTES];

    // gpio_get_level() reads gpio_level[], which is all high, as if pulled
    // up, until a test pulls a pin low.
    int gpio_level[GPIO_NUM_MAX];
    gpio_config_t gpio_config;

    // The SPI bus and device set up, and the last transaction sent.
    spi_bus_config_t spi_bus;
    spi_device_interface_config_t spi_device;
    spi_transaction_t spi_sent;
    int spi_transactions;

    // How the ADC was set up for continuous (DMA) sampling.
    adc_continuous_handle_cfg_t adc_handle_config;
    adc_continuous_config_t adc_config;
    adc_digi_pattern_config_t adc_pattern;
    bool adc_running;

    // adc_continuous_read() hands out the results queued by fake_adc_queue()
    // in order, at most adc_read_chunk per call, and times out once they run
    // out.  adc_read counts the results handed out so far.
    adc_digi_output_data_t adc_results[FAKE_ADC_MAX_RESULTS];
    int adc_queued;
    int adc_read;
    int adc_read_chunk;

    // How the ADC was set up for oneshot readings.  adc_oneshot_read() returns
    // adc_oneshot_raw[] for the channel it reads, but like the real driver, it
    // fails while the ADC is sampling continuously.
    adc_oneshot_unit_init_cfg_t adc_oneshot_unit;
    adc_channel_t adc_oneshot_channel;
    adc_oneshot_chan_cfg_t adc_oneshot_channel_config;
    int adc_oneshot_raw[FAKE_ADC_CHANNELS];
    int adc_oneshot_reads;

    // Whether the chip's eFuse holds ADC calibration.  The fake calibration
    // converts each raw reading to the same number of mV, so tests can queue
    // readings in mV.
    bool adc_cali_in_efuse;

    // The last bytes sent to the LED strip, and whether the firmware has yet
    // to wait for them to finish sending.
    uint8_t rmt_sent[FAKE_RMT_MAX_BYTES];
    size_t rmt_sent_bytes;
    int rmt_transmits;
    bool rmt_busy;
} fake_idf_t;

extern fake_idf_t fake;

// Put every fake back to how it is on a freshly booted board.
void fake_idf_reset(void);

// Queue one conversion result for adc_continuous_read() to hand out.
void fake_adc_queue(adc_channel_t channel, int raw);
