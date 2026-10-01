#pragma once

#include <stdint.h>

typedef enum {
    ADC_UNIT_1,
    ADC_UNIT_2,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_12  = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_DEFAULT = 0,
    ADC_BITWIDTH_12 = 12,
} adc_bitwidth_t;

typedef enum {
    ADC_CONV_SINGLE_UNIT_1 = 1,
    ADC_CONV_SINGLE_UNIT_2 = 2,
} adc_digi_convert_mode_t;

typedef enum {
    ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    ADC_DIGI_OUTPUT_FORMAT_TYPE2,
} adc_digi_output_format_t;

typedef struct {
    uint8_t atten;
    uint8_t channel;
    uint8_t unit;
    uint8_t bit_width;
} adc_digi_pattern_config_t;

// The ESP32-C3's layout of one DMA conversion result.
typedef struct {
    union {
        struct {
            uint32_t data:          12;
            uint32_t reserved12:    1;
            uint32_t channel:       3;
            uint32_t unit:          1;
            uint32_t reserved17_31: 15;
        } type2;
        uint32_t val;
    };
} adc_digi_output_data_t;
