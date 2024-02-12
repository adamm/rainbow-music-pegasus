#pragma once

#ifdef CONFIG_IDF_TARGET_ESP32
#define CONFIG_ADC_UNIT                ADC_UNIT_2
#define CONFIG_ADC_CHANNEL             ADC_CHANNEL_0
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define CONFIG_ADC_UNIT                ADC_UNIT_1
#define CONFIG_ADC_CHANNEL             ADC_CHANNEL_4   // GPIO 4
#endif
#define CONFIG_ADC_ATTEN               ADC_ATTEN_DB_12

#define CONFIG_GPIO_RGB_DATA           3

#define CONFIG_GPIO_TOTAL_LEDS_ADD_2   7
#define CONFIG_GPIO_TOTAL_LEDS_ADD_4   6
#define CONFIG_GPIO_TOTAL_LEDS_ADD_8   5

#define CONFIG_MIN_LEDS               10
#define CONFIG_MAX_LEDS               24

extern uint8_t _config_total_leds;

void config_init();
