
#include <Adafruit_NeoPixel.h>
#include <Adafruit_ZeroFFT.h>
#include <math.h>


#define LED_PIN     0
#define MIC_PIN     1
#define LED_COUNT   6
#define DATA_SIZE   64
#define FS          8000 // Sample Rate


int16_t data[DATA_SIZE];
int16_t decay[DATA_SIZE];
uint16_t hue_offset = 0;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() 
{
    Serial.begin(115200);
    analogReadResolution(10);
    pinMode(MIC_PIN, INPUT);

    strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
    strip.show();            // Turn OFF all pixels ASAP
    strip.setBrightness(0);
}

void loop() 
{
    int32_t i = 0;
    int32_t avg = 0;

    for (i = 0; i < DATA_SIZE; i++) {
        int16_t val = analogRead(MIC_PIN);
        avg += val;
        data[i] = val;
    }
    avg = avg / DATA_SIZE;
    for (i = 0; i < DATA_SIZE; i++) {
        data[i] = (data[i] - avg) * 32;
    }

    ZeroFFT(data, DATA_SIZE);

    for (i = 0; i < DATA_SIZE; i++) {
        data[i] >>= 4;
        if (data[i] <= 4)
            data[i] = 0;
        data[i] = pow(data[i], 2);
    }

    for (i = 0; i < DATA_SIZE/2; i++) {
        // Serial.print(FFT_BIN(i, FS, DATA_SIZE));
        // Serial.print(" Hz: ");
        int16_t val = (data[i]);
        if (val > 0)
            Serial.printf("%3d", val);
        else
            Serial.print("   ");

    }
    Serial.println("|");

    for (i = 0; i < DATA_SIZE; i++) {
        if ((data[i] == 0 ||
            data[i] < decay[i]) &&
            (decay[i] > 0)) {
            data[i] = decay[i] - 1;
        }
    }

    strip.setBrightness(255);
    uint32_t color;
    uint16_t r, g, b;
    int j = 0, k = 0;
    for (j = 0, i = 0; j < 6; j++) {
        r = 0;
        for (k = 0; k < 3; k++) {
            r += data[i+k];
            g += data[i+k+1];
            b += data[i+k+2];
        }
        color = strip.Color((uint8_t)r, (uint8_t)g, (uint8_t)b);
        strip.setPixelColor(j, color);
        i += 3;
    }
    memcpy(decay, data, sizeof(int16_t)*DATA_SIZE);

    for (i = 0; i < DATA_SIZE/2; i++) {
        // Serial.print(FFT_BIN(i, FS, DATA_SIZE));
        // Serial.print(" Hz: ");
        int16_t val = (decay[i]);
        if (val > 0)
            Serial.printf("%3d", val);
        else
            Serial.print("   ");
    }
    Serial.println("*");

    strip.show();                          //  Update strip to match
}
