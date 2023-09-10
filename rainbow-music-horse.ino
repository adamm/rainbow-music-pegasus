
#include <arduinoFFT.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino_APA102.h>

#define NUM_PIXELS 8
#define PIXELS_PIN 0
#define MIC_PIN 1

arduinoFFT FFT;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, PIXELS_PIN, NEO_GRB + NEO_KHZ800);
Arduino_APA102 led(1, INTERNAL_DS_DATA, INTERNAL_DS_CLK);

const uint16_t samples = 32;            //This value MUST ALWAYS be a power of 2
const double samplingFrequency = 2500;  //Hz, must be less than 10000 due to ADC

unsigned int sampling_period_us;
unsigned long microseconds;

double vReal[samples];
double vImag[samples];
double vDecay[samples];

enum {
  SCL_INDEX,
  SCL_TIME,
  SCL_FREQUENCY,
};

void setup() {
  // Shut off onboard APA102 on Trinket M0
  led.begin();
  led.setPixelColor(0, 0, 0, 0);
  led.show();

  strip.begin();
  memset(vDecay, 0, sizeof(double) * samples);
  sampling_period_us = round(1000000 * (1.0 / samplingFrequency));
  Serial.begin(115200);
}


void loop() {
  /*SAMPLING*/
  microseconds = micros();
  for (int i = 0; i < samples; i++) {
    vReal[i] = analogRead(MIC_PIN);
    vImag[i] = 0;
    while (micros() - microseconds < sampling_period_us) {
      //empty loop
    }
    microseconds += sampling_period_us;
  }
  FFT = arduinoFFT(vReal, vImag, samples, samplingFrequency); /* Create FFT object */
  /* Print the results of the sampling according to time */
  //  Serial.println("Data:");
  //  PrintVector(vReal, samples, SCL_TIME);
  FFT.DCRemoval();
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  //  Serial.println("Weighed data:");
  //  PrintVector(vReal, samples, SCL_TIME);
  FFT.Compute(FFT_FORWARD);
  //  Serial.println("Computed Real values:");
  //  PrintVector(vReal, samples, SCL_INDEX);
  //  Serial.println("Computed Imaginary values:");
  //  PrintVector(vImag, samples, SCL_INDEX);
  FFT.ComplexToMagnitude();

  uint8_t colour[samples] = { 0 };

  for (int i = 0; i < samples; i++) {
    strip.setPixelColor(i, 0, 0, 0);
    vReal[i] = abs(vReal[i]);
    if (vReal[i] < 100)
      vReal[i] = 0;
    else
      vReal[i] -= 100;

    if (vReal[i] > 500)
      colour[i] = 255;
    else
      colour[i] = vReal[i] / 2;
  }

  for (int i = 0, j = 0; i < samples && j < NUM_PIXELS / 2; i += 6) {
    strip.setPixelColor(j, colour[i], colour[i + 1], colour[i + 2]);
    strip.setPixelColor(NUM_PIXELS - j, colour[i + 3], colour[i + 4], colour[i + 5]);
    j++;
  }
  strip.show();

  //  PrintVector(vReal, (samples >> 1), SCL_FREQUENCY);
}

void PrintVector(double *vData, uint16_t bufferSize, uint8_t scaleType) {
  for (uint16_t i = 0; i < bufferSize; i++) {
    double abscissa;
    /* Print abscissa value */
    switch (scaleType) {
      case SCL_INDEX:
        abscissa = (i * 1.0);
        break;
      case SCL_TIME:
        abscissa = ((i * 1.0) / samplingFrequency);
        break;
      case SCL_FREQUENCY:
        abscissa = ((i * 1.0 * samplingFrequency) / samples);
        break;
    }
    Serial.print(abscissa, 1);
    if (scaleType == SCL_FREQUENCY)
      Serial.print("Hz");
    Serial.print(":");
    Serial.print(vData[i], 2);
    Serial.print(",");
  }
  Serial.println();
}
