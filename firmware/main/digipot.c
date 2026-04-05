
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "config.h"
#include "digipot.h"

const static char *TAG = "digipot";
static spi_device_handle_t spi;


void digipot_init() {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing bus SPI...");
    spi_bus_config_t buscfg = {
        //.miso_io_num = CONFIG_GPIO_DIGIPOT_MISO,  // not used
        .mosi_io_num = CONFIG_GPIO_DIGIPOT_MOSI,
        .sclk_io_num = CONFIG_GPIO_DIGIPOT_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,     //Clock out at 10 MHz
        .mode = 0,
        .spics_io_num = CONFIG_GPIO_DIGIPOT_CS,
        .queue_size = 7,
    };
    //Initialize the SPI bus
    ret = spi_bus_initialize(SPI1_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI1_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
}


void digipot_set_value(int value) {
}


void digipot_stop() {
    spi_bus_remove_device(spi);
}