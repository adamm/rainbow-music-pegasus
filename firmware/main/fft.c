// A fixed-point FFT of the mic's samples.  The ESP32-C3 has no FPU, so every
// float operation is a library call of about a hundred cycles, but it
// multiplies integers in one.  A float FFT of a 512-sample frame took 18.7 ms
// of the 30.7 ms it takes to record one.
//
// Samples are in mV, at most 2^12 from their mean, and are scaled by 2^17 so
// the windowed frame uses up to 2^29 of an int32's 2^31.  Each stage of the
// FFT halves its outputs, so they never grow larger than its inputs, and the
// sums in a stage, up to twice that, still fit.  The rounding this costs is a
// few parts in 2^17 of a mV, far under the noise floor.
#include <assert.h>
#include <math.h>

#include "fft.h"

// Samples are scaled by 2^SAMPLE_SHIFT, and the window and twiddle factors,
// fractions up to 1, by 2^WINDOW_SHIFT and 2^TWIDDLE_SHIFT.  The mean is
// taken in 1/2^MEAN_SHIFT mV, so removing it leaves no DC behind.
#define SAMPLE_SHIFT   17
#define WINDOW_SHIFT   15
#define TWIDDLE_SHIFT  30
#define MEAN_SHIFT     8

static uint16_t fft_samples = 0;
// The Blackman window's weights for the first half of the frame, the second
// half being their mirror image.
static int32_t window[FFT_MAX_SAMPLES / 2];
// cos and sin of 2 pi k / fft_samples, for k up to half the frame.
static int32_t twiddle_cos[FFT_MAX_SAMPLES / 2];
static int32_t twiddle_sin[FFT_MAX_SAMPLES / 2];
// The frame being transformed, in place.
static int32_t re[FFT_MAX_SAMPLES];
static int32_t im[FFT_MAX_SAMPLES];
// What a bin's magnitude is multiplied by for the amplitude in mV of a sine at
// its middle that would produce it.
static float magnitude_to_mv;


// Get ready for frames of `samples`, a power of 2 up to FFT_MAX_SAMPLES.
// The window and twiddle factors take double-precision cos() and sin() calls,
// too slow to make every frame, so they're worked out here, once.
void fft_init(uint16_t samples)
{
    assert(samples >= 2 && samples <= FFT_MAX_SAMPLES && (samples & (samples - 1)) == 0);
    fft_samples = samples;

    // The Blackman window, with the coefficients of the float FFT this replaced.
    int64_t window_sum = 0;
    for (int i = 0; i < samples / 2; i++) {
        double ratio = (double)i / (samples - 1);
        double weight = 0.42323 - 0.49755 * cos(2 * M_PI * ratio) + 0.07922 * cos(4 * M_PI * ratio);
        window[i] = (int32_t)lround(weight * (1 << WINDOW_SHIFT));
        window_sum += 2 * window[i];
    }

    for (int k = 0; k < samples / 2; k++) {
        twiddle_cos[k] = (int32_t)lround(cos(2 * M_PI * k / samples) * (1 << TWIDDLE_SHIFT));
        twiddle_sin[k] = (int32_t)lround(sin(2 * M_PI * k / samples) * (1 << TWIDDLE_SHIFT));
    }

    // The stages' halving divides the sums by `samples`, and a sine of
    // amplitude A at a bin's middle sums to A / 2 times the window's weights.
    float window_gain = (float)window_sum / (1 << WINDOW_SHIFT) / samples;
    magnitude_to_mv = 1.0f / ((1 << (SAMPLE_SHIFT - 1)) * window_gain);
}


// Load a frame of fft_init()'s size, in mV, removing its DC level, the mic's
// bias at about half the ADC's range, and applying the window.
void fft_load(const int* samples_mv)
{
    int n = fft_samples;
    int64_t sum = 0;
    for (int i = 0; i < n; i++)
        sum += samples_mv[i];
    int32_t mean = (int32_t)(sum * (1 << MEAN_SHIFT) / n);

    for (int i = 0; i < n / 2; i++) {
        int32_t first = samples_mv[i] * (1 << MEAN_SHIFT) - mean;
        int32_t last = samples_mv[n - 1 - i] * (1 << MEAN_SHIFT) - mean;
        re[i] = (int32_t)(((int64_t)first * window[i]) >> (MEAN_SHIFT + WINDOW_SHIFT - SAMPLE_SHIFT));
        re[n - 1 - i] = (int32_t)(((int64_t)last * window[i]) >> (MEAN_SHIFT + WINDOW_SHIFT - SAMPLE_SHIFT));
        im[i] = 0;
        im[n - 1 - i] = 0;
    }
}


// a times a twiddle factor.
static inline int32_t times_twiddle(int32_t a, int32_t twiddle)
{
    return (int32_t)(((int64_t)a * twiddle) >> TWIDDLE_SHIFT);
}


// Transform the loaded frame in place, dividing it by its size.
void fft_compute(void)
{
    int n = fft_samples;

    // Put the frame in bit-reversed order, so the butterflies work in place.
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    // Combine pairs of transforms of `half` samples into ones twice as long.
    for (int half = 1; half < n; half <<= 1) {
        int step = n / (2 * half);
        for (int j = 0; j < half; j++) {
            int32_t wr = twiddle_cos[j * step];
            int32_t wi = -twiddle_sin[j * step];
            for (int i = j; i < n; i += 2 * half) {
                int k = i + half;
                int32_t tr = times_twiddle(re[k], wr) - times_twiddle(im[k], wi);
                int32_t ti = times_twiddle(re[k], wi) + times_twiddle(im[k], wr);
                re[k] = (re[i] - tr) >> 1;
                im[k] = (im[i] - ti) >> 1;
                re[i] = (re[i] + tr) >> 1;
                im[i] = (im[i] + ti) >> 1;
            }
        }
    }
}


// Put the lowest `bins` bins of the transformed frame in bins_mv, each as the
// amplitude in mV of a sine at its middle that would produce it.  Each takes a
// square root, so compute only the bins that are used.
void fft_magnitudes(float* bins_mv, int bins)
{
    for (int i = 0; i < bins && i < fft_samples; i++) {
        float r = re[i];
        float m = im[i];
        bins_mv[i] = sqrtf(r * r + m * m) * magnitude_to_mv;
    }
}
