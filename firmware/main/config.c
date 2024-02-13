
#include <driver/gpio.h>
#include <esp_log.h>

#include "config.h"

const static char *TAG = "config";

uint8_t _config_total_leds = 0;
uint16_t _config_total_samples = 0;

void config_init() {
    // Set the total number of leds configured by looking at the jumpers JP5, JP6, JP7. 
    // If they are closed they'll force the GPIO low.
    gpio_config_t io_conf = {
        intr_type: GPIO_INTR_DISABLE,
        mode: GPIO_MODE_INPUT,
        pin_bit_mask: ((1ULL<<CONFIG_GPIO_TOTAL_LEDS_ADD_2) |
                       (1ULL<<CONFIG_GPIO_TOTAL_LEDS_ADD_4) |
                       (1ULL<<CONFIG_GPIO_TOTAL_LEDS_ADD_8)),
        pull_up_en: 1,
    };
    gpio_config(&io_conf);

    _config_total_leds = CONFIG_MIN_LEDS;

    ESP_LOGI(TAG, "Minimum is %d", CONFIG_MIN_LEDS);
    if (gpio_get_level(CONFIG_GPIO_TOTAL_LEDS_ADD_2) == 0) {
        ESP_LOGI(TAG, "CONFIG_GPIO_TOTAL_LEDS_ADD_2 pin %d (JP7) is low/active", CONFIG_GPIO_TOTAL_LEDS_ADD_2);
        _config_total_leds += 2;
    }
    if (gpio_get_level(CONFIG_GPIO_TOTAL_LEDS_ADD_4) == 0) {
        ESP_LOGI(TAG, "CONFIG_GPIO_TOTAL_LEDS_ADD_4 pin %d (JP6) is low/active", CONFIG_GPIO_TOTAL_LEDS_ADD_4);
        _config_total_leds += 4;
    }
    if (gpio_get_level(CONFIG_GPIO_TOTAL_LEDS_ADD_8) == 0) {
        ESP_LOGI(TAG, "CONFIG_GPIO_TOTAL_LEDS_ADD_8 pin %d (JP5) is low/active", CONFIG_GPIO_TOTAL_LEDS_ADD_8);
        _config_total_leds += 8;
    }

    if (_config_total_leds > CONFIG_MAX_LEDS)
        _config_total_leds = CONFIG_MAX_LEDS;

    if (_config_total_leds <= 10)
        _config_total_samples = 64;
    else if (_config_total_leds <= 16)
        _config_total_samples = 128;
    else
        _config_total_samples = 256;

    ESP_LOGI(TAG, "Total LEDs configured is %d", _config_total_leds);
    ESP_LOGI(TAG, "Total samples configured is %d", _config_total_samples);
}
