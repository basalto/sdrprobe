#ifndef GSM_DSP_H
#define GSM_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * GSM 900 technology plugin.
 *
 * A "technology plugin" is a small, testable DSP module for one cellular
 * technology. It provides two things and reuses the generic SDR primitives in
 * sdr_dsp.h for everything else (FFT/spectrum, centroid estimate, per-channel
 * power, PPM correction):
 *
 *   1. a channel -> frequency map           (gsm_downlink_hz)
 *   2. a reference-tone / sync detector      (gsm_fcch_detect)
 *
 * The scope is calibration-grade detection (identify the reference carrier and
 * measure its frequency), not full demodulation or message decoding.
 *
 * This file depends on nothing from sdr_dsp.h; it operates on raw centred I/Q.
 */

/* FCCH is an all-zeros GMSK burst: a pure tone at 1625/24 kHz above the
   carrier centre. */
#define GSM_FCCH_TONE_HZ (1625000.0 / 24.0)

struct gsm_fcch_result {
    int detected;
    double tone_frequency_hz; /* baseband; caller maps to the RF carrier */
    float confidence;         /* lag-1 autocorrelation coherence, [0, 1] */
    float amplitude;
};

/* GSM 900 downlink ARFCN (1-124) -> frequency in Hz. Returns 0 for an
   out-of-range ARFCN. */
int gsm_downlink_hz(unsigned int arfcn, uint32_t *frequency_hz);

/* Detect the FCCH pure tone near target_offset_hz (baseband) within
   +/- search_window_hz. Returns 1 (and sets result->detected) when the peak
   coherence meets the internal threshold; result->confidence carries the peak
   coherence found even when it does not. */
int gsm_fcch_detect(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    double target_offset_hz, double search_window_hz,
                    struct gsm_fcch_result *result);

#endif
