#ifndef __FFT_H__
#define __FFT_H__

#include <stdint.h>

// The largest frame fft_init() takes: 24 LEDs in patterns B and C.
#define FFT_MAX_SAMPLES 512

void fft_init(uint16_t samples);
void fft_load(const int* samples_mv);
void fft_compute(void);
void fft_magnitudes(float* bins_mv, int bins);

#endif
