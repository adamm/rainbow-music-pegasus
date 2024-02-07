#pragma once

#ifdef CONFIG_IDF_TARGET_ESP32
#define CONFIG_ADC_UNIT            ADC_UNIT_2
#define CONFIG_ADC_CHANNEL         ADC_CHANNEL_0
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define CONFIG_ADC_UNIT            ADC_UNIT_1
#define CONFIG_ADC_CHANNEL         ADC_CHANNEL_4   // GPIO 4
#endif
#define CONFIG_ADC_ATTEN           ADC_ATTEN_DB_12

#define CONFIG_LED_GPIO            3
#define CONFIG_LED_TOTAL           12
