#ifndef __LEDS_H__
#define __LEDS_H__

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_t;

typedef struct {
    led_t* led;
} leds_t;


void leds_init(void);
void leds_scanning_start(void);
void leds_scanning_stop(void);
void leds_display(uint8_t*, int);

#endif 