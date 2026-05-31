
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "config.h"
#include "digipot.h"

const static char *TAG = "digipot";
static spi_device_handle_t spi;

// MCP41050: command byte = write(01) | pot0(01) = 0x11, followed by 8-bit wiper value
#define MCP41050_CMD_WRITE  0x11

void digipot_init() {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing bus SPI...");
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,  // MCP41050 is write-only
        .mosi_io_num = CONFIG_GPIO_DIGIPOT_MOSI,
        .sclk_io_num = CONFIG_GPIO_DIGIPOT_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = CONFIG_GPIO_DIGIPOT_CS,
        .queue_size = 1,
    };
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
}


void digipot_set_value(int value) {
    if (value < 0)   value = 0;
    if (value > 255) value = 255;

    spi_transaction_t t = {
        .flags  = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = { MCP41050_CMD_WRITE, (uint8_t)value },
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}


void digipot_stop() {
    spi_bus_remove_device(spi);
    spi_bus_free(SPI2_HOST);
}