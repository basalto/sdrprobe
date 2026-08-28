#include "gsm_dsp.h"

#include <math.h>
#include <stdlib.h>

#define GSM_TWO_PI 6.283185307179586
#define GSM_FCCH_WINDOW_SECONDS 400e-6
#define GSM_FCCH_MIN_COHERENCE 0.9f

int gsm_downlink_hz(unsigned int arfcn, uint32_t *frequency_hz) {
    if (!frequency_hz || arfcn < 1 || arfcn > 124)
        return 0;
    *frequency_hz = 935000000U + arfcn * 200000U;
    return 1;
}

int gsm_fcch_detect(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    double target_offset_hz, double search_window_hz,
                    struct gsm_fcch_result *result) {
    if (!i_samples || !q_samples || !result || sample_rate <= 0.0 ||
        search_window_hz <= 0.0)
        return 0;

    result->detected = 0;
    result->tone_frequency_hz = 0.0;
    result->confidence = 0.0f;
    result->amplitude = 0.0f;

    size_t window = (size_t)llround(GSM_FCCH_WINDOW_SECONDS * sample_rate);
    if (window < 64)
        window = 64;
    if (window > pair_count)
        return 0;
    size_t hop = window / 4;
    if (hop < 1)
        hop = 1;

    double best_coherence = 0.0;
    double best_frequency = 0.0;
    double best_amplitude = 0.0;
    int found = 0;

    /* The FCCH burst is an all-zeros GMSK sequence, i.e. a pure tone. Over a
       window that falls inside the burst the lag-1 autocorrelation phase is
       constant, so the summed cross products stay coherent; modulated data and
       noise cancel. The coherence ratio is the toneness metric and the angle
       of the sum is a robust (amplitude-weighted) tone-frequency estimate. */
    for (size_t start = 0; start + window <= pair_count; start += hop) {
        double accum_re = 0.0;
        double accum_im = 0.0;
        double sum_mag = 0.0;
        double amplitude_sum = 0.0;
        double prev_mag = sqrt((double)i_samples[start] * i_samples[start] +
                               (double)q_samples[start] * q_samples[start]);
        for (size_t n = 1; n < window; n++) {
            float i1 = i_samples[start + n];
            float q1 = q_samples[start + n];
            float i0 = i_samples[start + n - 1];
            float q0 = q_samples[start + n - 1];
            double cross_re = (double)i1 * i0 + (double)q1 * q0;
            double cross_im = (double)q1 * i0 - (double)i1 * q0;
            accum_re += cross_re;
            accum_im += cross_im;
            double mag = sqrt((double)i1 * i1 + (double)q1 * q1);
            sum_mag += mag * prev_mag;
            amplitude_sum += mag;
            prev_mag = mag;
        }
        double accum_mag = sqrt(accum_re * accum_re + accum_im * accum_im);
        double coherence = sum_mag > 0.0 ? accum_mag / sum_mag : 0.0;
        double frequency = atan2(accum_im, accum_re) / GSM_TWO_PI * sample_rate;
        if (fabs(frequency - target_offset_hz) > search_window_hz)
            continue;
        if (!found || coherence > best_coherence) {
            best_coherence = coherence;
            best_frequency = frequency;
            best_amplitude = amplitude_sum / (double)(window - 1);
            found = 1;
        }
    }

    if (!found)
        return 0;
    result->tone_frequency_hz = best_frequency;
    result->confidence = (float)best_coherence;
    result->amplitude = (float)best_amplitude;
    result->detected = best_coherence >= GSM_FCCH_MIN_COHERENCE;
    return result->detected;
}
