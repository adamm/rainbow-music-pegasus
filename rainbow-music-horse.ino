
#include <Adafruit_NeoPixel.h>

#define LED_PIN     0
#define MIC_PIN     1
#define LED_COUNT   6

const int sampleWindow = 100; // Sample window width in mS (250 mS = 4Hz)

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
    unsigned long start= millis();  // Start of sample window
    unsigned int peakToPeak = 0;   // peak-to-peak level

    unsigned int signalMax = 0;
    unsigned int signalMin = 1024;

    // collect data for 250 miliseconds
    while (millis() - start < sampleWindow) {
        int knock = analogRead(MIC_PIN);
        if (knock < 1024) {  //This is the max of the 10-bit ADC so this loop will include all readings
            if (knock > signalMax) {
                signalMax = knock;  // save just the max levels
            }
            else if (knock < signalMin) {
                signalMin = knock;  // save just the min levels
            }
        }
    }
    Serial.print("analogRead: ");
    Serial.print(signalMin);
    Serial.print("-");
    Serial.print(signalMax);

    peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude
    double volts = (peakToPeak * 3.3) / 1024;  // convert to volts

    Serial.print(" volts: ");
    Serial.print(volts);

    double brightness = volts * 255 / 3.3;
    uint32_t color = strip.Color(255, 255, 255);

    strip.setBrightness((int)brightness);
    strip.setPixelColor(0, color);
    strip.setPixelColor(1, color);
    strip.setPixelColor(2, color);
    strip.setPixelColor(3, color);
    strip.setPixelColor(4, color);
    strip.setPixelColor(5, color);
    strip.show();                          //  Update strip to match

    Serial.print(" brightness: ");
    Serial.println(brightness);
}
