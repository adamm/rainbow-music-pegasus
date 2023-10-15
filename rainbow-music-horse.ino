/*

	Rainbow music horse 3D printed project
        Copyright (C) 2023 Adam McDaniel

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Uses code from the FFT_03 example of use of the FFT library to compute FFT
    for a signal sampled through the ADC.
        Copyright (C) 2018 Enrique Condés and Ragnar Ranøyen Homb
*/

#include <arduinoFFT.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino_APA102.h>

#define NUM_PIXELS 21
#define PIXELS_PIN 0
#define MIC_PIN 1

arduinoFFT FFT;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, PIXELS_PIN, NEO_GRB + NEO_KHZ800);
Arduino_APA102 led(1, INTERNAL_DS_DATA, INTERNAL_DS_CLK);

const uint16_t samples = 64;            //This value MUST ALWAYS be a power of 2
const double samplingFrequency = 5000;  //Hz, must be less than 10000 due to ADC

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


/* The below code applies the following pattern a 21-neopixel light strip,
 * with 9 LEDs on the left, 3 on the top, and 9 on the right:

      C1C2C3
      ______
  L1 |      | R1
  L2 |      | R2
  L3 |      | R3
  L4 |      | R4
  L5 |      | R5
  L6 |      | R6
  L7 |      | R7
  L8 |      | R8
  L9 |      | R9
     ⌃
  data in

  L1..L9 and R1..R9 display the frequency-colour registered by the FFT
  algorithm. The lower the L# or R#, the lower the frequency.

  TODO: C1..C3 should be a static colour to light up the whole horse.
*/

  int i = 0;
  for (int j = N_PIXELS/2+2; i < samples && j < N_PIXELS; i += 3) {
    strip.setPixelColor(j, colour[i], colour[i + 1], colour[i + 2]);
    j++;
  }
  i+=9;
  for (int j = 0; i < samples && j < (N_PIXELS / 2)-1; i += 3) {
    strip.setPixelColor(j, colour[i], colour[i + 1], colour[i + 2]);
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
