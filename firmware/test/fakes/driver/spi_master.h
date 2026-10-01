#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum { SPI1_HOST, SPI2_HOST } spi_host_device_t;
typedef enum { SPI_DMA_DISABLED = 0, SPI_DMA_CH_AUTO = 3 } spi_dma_chan_t;

typedef struct {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

typedef struct {
    uint8_t mode;
    int clock_speed_hz;
    int spics_io_num;
    int queue_size;
} spi_device_interface_config_t;

typedef struct spi_device_t *spi_device_handle_t;

#define SPI_TRANS_USE_TXDATA (1<<3)

typedef struct {
    uint32_t flags;
    size_t length;
    union {
        const void *tx_buffer;
        uint8_t tx_data[4];
    };
} spi_transaction_t;

esp_err_t spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config, spi_dma_chan_t dma_chan);
esp_err_t spi_bus_add_device(spi_host_device_t host_id, const spi_device_interface_config_t *dev_config, spi_device_handle_t *handle);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);
esp_err_t spi_bus_remove_device(spi_device_handle_t handle);
esp_err_t spi_bus_free(spi_host_device_t host_id);
