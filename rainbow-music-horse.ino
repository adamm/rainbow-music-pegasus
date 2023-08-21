
#include <Adafruit_NeoPixel.h>
#include <Adafruit_ZeroFFT.h>
#include <math.h>


#define LED_PIN     0
#define MIC_PIN     1
#define LED_COUNT   6
#define SAMPLE_SIZE 64
#define SPECTRUM_SIZE (SAMPLE_SIZE / 2)
#define NOISE_LIMIT 100
#define LOW_BIN     3
#define HIGH_BIN    1000

int16_t data[SAMPLE_SIZE];
int16_t decay[SAMPLE_SIZE];
float   spectrum[SPECTRUM_SIZE];

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() 
{
    Serial.begin(115200);
    analogReadResolution(12);
    pinMode(MIC_PIN, INPUT);

    strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
    strip.show();            // Turn OFF all pixels ASAP
    strip.setBrightness(0);
}


void print_data(int16_t* data) {
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        int16_t val = (data[i]);
        if (val > 0) {
            Serial.printf(" %4ld ", val);
        }
        else if (val < 0) {
            Serial.printf("%+5ld ", val);
        }
        else {
            Serial.print("      ");
        }

    }
    Serial.println("|");
}


void loop() 
{
    int32_t i = 0;
    int32_t avg = 0;

    for (i = 0; i < SAMPLE_SIZE; i++) {
        int16_t val = (analogRead(MIC_PIN) - 2048);
        if ((val > 0 && val < NOISE_LIMIT) ||
            (val < 0 && val > -NOISE_LIMIT))
            val = 0;
        data[i] = val;
        avg += val;
    }

    // Serial.print("raw:\t");
    // print_data(data);

    avg = avg / SAMPLE_SIZE;
    for (i = 0; i < SAMPLE_SIZE; i++) {
        if (data[i] != 0)
            data[i] = data[i] - avg;
    }

    // Serial.printf("avg:%-d", avg);
    // print_data(data);

    ZeroFFT(data, SAMPLE_SIZE);

    // Serial.print("fft:\t");
    // print_data(data);

    for (i = 0; i < SAMPLE_SIZE; i++) {
        if (data[i] <= LOW_BIN)
            data[i] = 0;
        if (data[i] >= HIGH_BIN)
            data[i] = HIGH_BIN;
        data[i] *= 3;
    }

    // Serial.print("filter:\t");
    // print_data(data);

    for (i = 0; i < SAMPLE_SIZE; i++) {
        if ((data[i] == 0 ||
            data[i] < decay[i]) &&
            (decay[i] > 0)) {
            // if (decay[i] > 1)
                data[i] = decay[i] - 1;
            // else
            //     data[i] = 0;
        }
    }

    strip.setBrightness(255);
    uint32_t color;
    uint16_t r, g, b;
    int j = 0, k = 0;
    for (j = 0, i = 0; j < 6; j++) {
        r = data[i];
        g = data[i+1];
        b = data[i+2];
        color = strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
        strip.setPixelColor(j, color);
        i += 3;
    }
    memcpy(decay, data, sizeof(int16_t)*SAMPLE_SIZE);

    // Serial.print("decay:\t");
    // print_data(decay);

    strip.show();                          //  Update strip to match

    // delay(1000);
}
