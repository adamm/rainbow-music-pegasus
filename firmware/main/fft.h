/*

	FFT library
	Copyright (C) 2010 Didier Longueville
	Copyright (C) 2014 Enrique Condes

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.    If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef FFT_H
#define FFT_H
#include <stdio.h>
#include <stdlib.h>

#include <math.h>


typedef enum {
    Reverse, Forward
} FFTDirection;

typedef enum {
    Rectangle,
    Hamming,
    Hann,
    Triangle,
    Nuttall,
    Blackman,
    Blackman_Nuttall,
    Blackman_Harris,
    Flat_top,
    Welch
} FFTWindow;

/* Custom constants */
#define FFT_FORWARD Forward
#define FFT_REVERSE Reverse

/* Windowing type */
#define FFT_WIN_TYP_RECTANGLE           Rectangle
#define FFT_WIN_TYP_HAMMING             Hamming
#define FFT_WIN_TYP_HANN                Hann
#define FFT_WIN_TYP_TRIANGLE            Triangle
#define FFT_WIN_TYP_NUTTALL             Nuttall
#define FFT_WIN_TYP_BLACKMAN            Blackman
#define FFT_WIN_TYP_BLACKMAN_NUTTALL    Blackman_Nuttall
#define FFT_WIN_TYP_BLACKMAN_HARRIS     Blackman_Harris
#define FFT_WIN_TYP_FLT_TOP             Flat_top
#define FFT_WIN_TYP_WELCH               Welch
/*Mathematial constants*/
#define twoPi 6.28318531
#define fourPi 12.56637061
#define sixPi 18.84955593

void fft_init(float *vReal, float *vImag, uint16_t samples,
                     float samplingFrequency);

void fft_complexToMagnitude();
void fft_compute(FFTDirection dir);
void fft_dcRemoval();
float fft_majorPeak();
void fft_windowing(FFTWindow windowType, FFTDirection dir);
float fft_MajorPeakParabola();


#endif
