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

#include "fft.h"

#define sq(x) ((x)*(x))

uint16_t _samples;
float _samplingFrequency;
float *_vReal;
float *_vImag;
uint8_t _power;

static uint8_t exponent(uint16_t value);
static void swap(float *x, float *y);
static void parabola(float x1, float y1, float x2, float y2, float x3,
                            float y3, float *a, float *b, float *c);

void fft_init(float *vReal, float *vImag, uint16_t samples,
                                             float samplingFrequency) { // Constructor
    _vReal = vReal;
    _vImag = vImag;
    _samples = samples;
    _samplingFrequency = samplingFrequency;
    _power = exponent(samples);
}

void fft_compute(FFTDirection dir) {
    // Computes in-place complex-to-complex FFT /
    // Reverse bits /
    uint16_t j = 0;
    for (uint16_t i = 0; i < (_samples - 1); i++) {
        if (i < j) {
            swap(&_vReal[i], &_vReal[j]);
            if (dir == FFT_REVERSE)
                swap(&_vImag[i], &_vImag[j]);
        }
        uint16_t k = (_samples >> 1);
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    float c1 = -1.0;
    float c2 = 0.0;
    uint16_t l2 = 1;
    for (uint8_t l = 0; (l < _power); l++) {
        uint16_t l1 = l2;
        l2 <<= 1;
        float u1 = 1.0;
        float u2 = 0.0;
        for (j = 0; j < l1; j++) {
            for (uint16_t i = j; i < _samples; i += l2) {
                uint16_t i1 = i + l1;
                float t1 = u1 * _vReal[i1] - u2 * _vImag[i1];
                float t2 = u1 * _vImag[i1] + u2 * _vReal[i1];
                _vReal[i1] = _vReal[i] - t1;
                _vImag[i1] = _vImag[i] - t2;
                _vReal[i] += t1;
                _vImag[i] += t2;
            }
            float z = ((u1 * c1) - (u2 * c2));
            u2 = ((u1 * c2) + (u2 * c1));
            u1 = z;
        }
        c2 = sqrt((1.0 - c1) / 2.0);
        c1 = sqrt((1.0 + c1) / 2.0);
        if (dir == FFT_FORWARD) {
            c2 = -c2;
        }
    }
    // Scaling for reverse transform /
    if (dir != FFT_FORWARD) {
        float reciprocal = 1.0 / _samples;
        for (uint16_t i = 0; i < _samples; i++) {
            _vReal[i] *= reciprocal;
            _vImag[i] *= reciprocal;
        }
    }
}


void fft_complexToMagnitude() {
    // vM is half the size of vReal and vImag
    for (uint16_t i = 0; i < _samples; i++) {
        _vReal[i] = sqrt(sq(_vReal[i]) + sq(_vImag[i]));
    }
}


void fft_dcRemoval() {
    // calculate the mean of vData
    float mean = 0;
    for (uint16_t i = 0; i < _samples; i++) {
        mean += _vReal[i];
    }
    mean /= _samples;
    // Subtract the mean from vData
    for (uint16_t i = 0; i < _samples; i++) {
        _vReal[i] -= mean;
    }
}


void fft_windowing(FFTWindow windowType, FFTDirection dir) {
    // Weighing factors are computed once before multiple use of FFT
    // The weighing function is symmetric; half the weighs are recorded
    float samplesMinusOne = (float)(_samples - 1.0);
    for (uint16_t i = 0; i < (_samples >> 1); i++) {
        float indexMinusOne = (float)i;
        float ratio = (indexMinusOne / samplesMinusOne);
        float weighingFactor = 1.0;
        // Compute and record weighting factor
        switch (windowType) {
        case FFT_WIN_TYP_RECTANGLE: // rectangle (box car)
            weighingFactor = 1.0;
            break;
        case FFT_WIN_TYP_HAMMING: // hamming
            weighingFactor = 0.54 - (0.46 * cos(twoPi * ratio));
            break;
        case FFT_WIN_TYP_HANN: // hann
            weighingFactor = 0.54 * (1.0 - cos(twoPi * ratio));
            break;
        case FFT_WIN_TYP_TRIANGLE: // triangle (Bartlett)
#if defined(ESP8266) || defined(ESP32)
            weighingFactor =
                    1.0 - ((2.0 * fabs(indexMinusOne - (samplesMinusOne / 2.0))) /
                                 samplesMinusOne);
#else
            weighingFactor =
                    1.0 - ((2.0 * fabs(indexMinusOne - (samplesMinusOne / 2.0))) /
                                 samplesMinusOne);
#endif
            break;
        case FFT_WIN_TYP_NUTTALL: // nuttall
            weighingFactor = 0.355768 - (0.487396 * (cos(twoPi * ratio))) +
                                             (0.144232 * (cos(fourPi * ratio))) -
                                             (0.012604 * (cos(sixPi * ratio)));
            break;
        case FFT_WIN_TYP_BLACKMAN: // blackman
            weighingFactor = 0.42323 - (0.49755 * (cos(twoPi * ratio))) +
                                             (0.07922 * (cos(fourPi * ratio)));
            break;
        case FFT_WIN_TYP_BLACKMAN_NUTTALL: // blackman nuttall
            weighingFactor = 0.3635819 - (0.4891775 * (cos(twoPi * ratio))) +
                                             (0.1365995 * (cos(fourPi * ratio))) -
                                             (0.0106411 * (cos(sixPi * ratio)));
            break;
        case FFT_WIN_TYP_BLACKMAN_HARRIS: // blackman harris
            weighingFactor = 0.35875 - (0.48829 * (cos(twoPi * ratio))) +
                                             (0.14128 * (cos(fourPi * ratio))) -
                                             (0.01168 * (cos(sixPi * ratio)));
            break;
        case FFT_WIN_TYP_FLT_TOP: // flat top
            weighingFactor = 0.2810639 - (0.5208972 * cos(twoPi * ratio)) +
                                             (0.1980399 * cos(fourPi * ratio));
            break;
        case FFT_WIN_TYP_WELCH: // welch
            weighingFactor = 1.0 - sq((indexMinusOne - samplesMinusOne / 2.0) /
                                                                (samplesMinusOne / 2.0));
            break;
        }
        if (dir == FFT_FORWARD) {
            _vReal[i] *= weighingFactor;
            _vReal[_samples - (i + 1)] *= weighingFactor;
        } else {
            _vReal[i] /= weighingFactor;
            _vReal[_samples - (i + 1)] /= weighingFactor;
        }
    }
}

float fft_majorPeak() {
    float maxY = 0;
    uint16_t IndexOfMaxY = 0;
    // If sampling_frequency = 2 * max_frequency in signal,
    // value would be stored at position samples/2
    for (uint16_t i = 1; i < ((_samples >> 1) + 1); i++) {
        if ((_vReal[i - 1] < _vReal[i]) &&
                (_vReal[i] > _vReal[i + 1])) {
            if (_vReal[i] > maxY) {
                maxY = _vReal[i];
                IndexOfMaxY = i;
            }
        }
    }
    float delta =
            0.5 *
            ((_vReal[IndexOfMaxY - 1] - _vReal[IndexOfMaxY + 1]) /
             (_vReal[IndexOfMaxY - 1] - (2.0 * _vReal[IndexOfMaxY]) +
                _vReal[IndexOfMaxY + 1]));
    float interpolatedX =
            ((IndexOfMaxY + delta) * _samplingFrequency) / (_samples - 1);
    if (IndexOfMaxY ==
            (_samples >> 1)) // To improve calculation on edge values
        interpolatedX =
                ((IndexOfMaxY + delta) * _samplingFrequency) / (_samples);
    // returned value: interpolated frequency peak apex
    return (interpolatedX);
}


float fft_majorPeakParabola() {
    float maxY = 0;
    uint16_t IndexOfMaxY = 0;
    // If sampling_frequency = 2 * max_frequency in signal,
    // value would be stored at position samples/2
    for (uint16_t i = 1; i < ((_samples >> 1) + 1); i++) {
        if ((_vReal[i - 1] < _vReal[i]) &&
                (_vReal[i] > _vReal[i + 1])) {
            if (_vReal[i] > maxY) {
                maxY = _vReal[i];
                IndexOfMaxY = i;
            }
        }
    }

    float freq = 0;
    if (IndexOfMaxY > 0) {
        // Assume the three points to be on a parabola
        float a, b, c;
        parabola(IndexOfMaxY - 1, _vReal[IndexOfMaxY - 1], IndexOfMaxY,
                         _vReal[IndexOfMaxY], IndexOfMaxY + 1,
                         _vReal[IndexOfMaxY + 1], &a, &b, &c);

        // Peak is at the middle of the parabola
        float x = -b / (2 * a);

        // And magnitude is at the extrema of the parabola if you want It...
        // float y = a*x*x+b*x+c;

        // Convert to frequency
        freq = (x * _samplingFrequency) / (_samples);
    }

    return freq;
}

static void parabola(float x1, float y1, float x2, float y2, float x3,
                                                    float y3, float *a, float *b, float *c) {
    float reversed_denom = 1 / ((x1 - x2) * (x1 - x3) * (x2 - x3));

    *a = (x3 * (y2 - y1) + x2 * (y1 - y3) + x1 * (y3 - y2)) * reversed_denom;
    *b = (x3 * x3 * (y1 - y2) + x2 * x2 * (y3 - y1) + x1 * x1 * (y2 - y3)) *
             reversed_denom;
    *c = (x2 * x3 * (x2 - x3) * y1 + x3 * x1 * (x3 - x1) * y2 +
                x1 * x2 * (x1 - x2) * y3) *
             reversed_denom;
}

static uint8_t exponent(uint16_t value) {
    // Calculates the base 2 logarithm of a value
    uint8_t result = 0;
    while (value >>= 1)
        result++;
    return (result);
}

// Private functions

static void swap(float *x, float *y) {
    float temp = *x;
    *x = *y;
    *y = temp;
}
