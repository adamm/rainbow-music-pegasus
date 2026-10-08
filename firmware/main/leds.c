/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/rmt_tx.h"

#include "config.h"
#include "led_strip_encoder.h"
#include "leds.h"


#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)

// While the battery is low, the first LED in each wing blinks red over the light
// show, for LEDS_LOW_BATTERY_ON_US every LEDS_LOW_BATTERY_PERIOD_US.  In pattern
// A the light show never lights both LEDs of a pair pure red, so the blink can't
// be mistaken for the music, and every other LED shows the music as usual.  In
// patterns B and C, deep bass alone lights the first pair pure red.
#define LEDS_LOW_BATTERY_PERIOD_US  3000000
#define LEDS_LOW_BATTERY_ON_US      250000
#define LEDS_LOW_BATTERY_RED        128

static const char *TAG = "leds";

static uint8_t led_strip_pixels[CONFIG_MAX_LEDS * 3];
static TaskHandle_t scanning_task = NULL;
static bool low_battery = false;

/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

rmt_channel_handle_t led_chan = NULL;
rmt_encoder_handle_t led_encoder = NULL;

void leds_init(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = CONFIG_GPIO_RGB_DATA,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_ERROR_CHECK(rmt_enable(led_chan));
}


static void leds_scanning() {
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };
    led_t colours[CONFIG_MAX_LEDS];

    for (;;) {
        for (int leader = 0; leader < _config_total_leds+4; leader++) {
            for (int i = 0; i < _config_total_leds; i += 2) {
                colours[i] = (led_t){0};
                uint8_t strength = 100;
                if (i == leader) {
                    colours[i].red   = strength / 4;
                    colours[i].green = strength / 4;
                    colours[i].blue  = strength / 4;
                }
                else if ((i == leader - 1) && i >= 0) {
                    colours[i].red   = strength / 8;
                    colours[i].green = strength / 8;
                    colours[i].blue  = strength / 8;
                }
                else if ((i == leader - 2) && i >= 0) {
                    colours[i].red   = strength / 16;
                    colours[i].green = strength / 16;
                    colours[i].blue  = strength / 16;
                }
                else if ((i == leader - 3) && i >= 0) {
                    colours[i].red   = strength / 32;
                    colours[i].green = strength / 32;
                    colours[i].blue  = strength / 32;
                }
                else {
                    colours[i].red   = 0;
                    colours[i].green = 0;
                    colours[i].blue  = 0;
                }
                led_strip_pixels[i*3+0] = colours[i].red;
                led_strip_pixels[i*3+1] = colours[i].green;
                led_strip_pixels[i*3+2] = colours[i].blue;
                led_strip_pixels[i*3+3] = colours[i].red;
                led_strip_pixels[i*3+4] = colours[i].green;
                led_strip_pixels[i*3+5] = colours[i].blue;
            }
            // for (int j = 0; j < CONFIG_MAX_LEDS * 3; j++ ) {
            //     printf("%d ", led_strip_pixels[j]);
            // }
            // printf("\n");
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            vTaskDelay(pdMS_TO_TICKS(75));
        }
    }
}


void leds_scanning_start() {
    ESP_LOGI(TAG, "Starting scanning task");
    xTaskCreate(leds_scanning, "scanning", 8192*3, NULL, 5, &scanning_task);
}


void leds_scanning_stop() {
    ESP_LOGI(TAG, "Stopping scanning task");
    vTaskDelete(scanning_task);
}


// Set the LED at `index` along the strip.  WS2812Bs take the bytes as green,
// red, blue.
static void leds_set(int index, uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_pixels[index*3]   = green;
    led_strip_pixels[index*3+1] = red;
    led_strip_pixels[index*3+2] = blue;
}


// The brightness of bin i of the total_values in values, dark past the end.
static uint8_t leds_bin(const uint8_t* values, int total_values, int i) {
    return i < total_values ? values[i] : 0;
}


// Pattern A: each pair of LEDs shows three bins, lowest first.
static void leds_display_pattern_a(uint8_t* values, int total_values) {
    for (int i = 0; (i < total_values) && (i < _config_total_leds*3/2); i += 3) {
        // Right side gets the green byte first.  Left side gets the blue byte first.
        // This results in a symmetrical brightness but asymmetrical colour when comparing
        // right-side and left-side LEDs.
        //
        // "Thin" tones that span single FFT column will produce red, green, and blue.
        // "Thick" tones that span multiple FFT column will produce yellow, cyan, magenta,
        // "Very wide" and complex tones will produce white.

        led_strip_pixels[i*2]   = values[i];    // right-side green
        led_strip_pixels[i*2+1] = values[i+1];  // right-side red
        led_strip_pixels[i*2+2] = values[i+2];  // right-side blue
        led_strip_pixels[i*2+3] = values[i+1];  // left-side green
        led_strip_pixels[i*2+4] = values[i+2];  // left-side red
        led_strip_pixels[i*2+5] = values[i];    // left-side blue

        // Right green sounds will be blue on the left.
        // Right red sounds will be green on the left.
        // Right blue sounds will be red on the left.
        // Right magenta sounds will be yellow on the left.
        // Right yellow sounds will be cyan on the left.
        // Right cyan sounds will be magenta on the left.
        // Right white sounds will be white on the left.

        // Pretty neat!
    }
}


// Pattern B: each pair of LEDs shows six bins, one per channel, lowest first:
// the right then left LED's red, then their green, then their blue.  So each
// pair runs from red to blue, and the wings show alternate bins.
static void leds_display_pattern_b(uint8_t* values, int total_values) {
    for (int i = 0; i < _config_total_leds; i++) {
        int first = i / 2 * 6 + i % 2;
        leds_set(i,
                 leds_bin(values, total_values, first),
                 leds_bin(values, total_values, first + 2),
                 leds_bin(values, total_values, first + 4));
    }
}


// Pattern C: the lowest third of the bins light every LED's red in strip
// order, the middle third their green, and the top third their blue.  So each
// colour shows a third of the frequency range along the wings, and the wings
// show alternate bins.
static void leds_display_pattern_c(uint8_t* values, int total_values) {
    int leds = _config_total_leds;

    for (int i = 0; i < leds; i++) {
        leds_set(i,
                 leds_bin(values, total_values, i),
                 leds_bin(values, total_values, leds + i),
                 leds_bin(values, total_values, leds * 2 + i));
    }
}


void leds_display(uint8_t* values, int total_values) {
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };

    switch (_config_pattern) {
    case CONFIG_PATTERN_A:
        leds_display_pattern_a(values, total_values);
        break;
    case CONFIG_PATTERN_B:
        leds_display_pattern_b(values, total_values);
        break;
    case CONFIG_PATTERN_C:
        leds_display_pattern_c(values, total_values);
        break;
    }

    // The first pair of LEDs is the nearest the ESP32, and is fitted on every board.
    if (low_battery && esp_timer_get_time() % LEDS_LOW_BATTERY_PERIOD_US < LEDS_LOW_BATTERY_ON_US) {
        for (int i = 0; i < 2; i++) {
            led_strip_pixels[i*3]   = 0;                     // green
            led_strip_pixels[i*3+1] = LEDS_LOW_BATTERY_RED;  // red
            led_strip_pixels[i*3+2] = 0;                     // blue
        }
    }

    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
}


void leds_show_low_battery(bool low) {
    low_battery = low;
}
