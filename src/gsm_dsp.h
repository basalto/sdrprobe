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

/*
 * Synchronisation Channel (SCH) decode.
 *
 * The SCH is a modulated, coded burst on the BCCH carrier (GSM 05.02/05.03).
 * Decoding it goes beyond calibration-grade detection: it differential-demods
 * the GMSK burst, syncs on the 64-bit extended training sequence, Viterbi
 * decodes the rate-1/2 K=5 convolutional code, checks the 10-bit parity, and
 * parses the 25 information bits into the BSIC (NCC + BCC) and the reduced
 * TDMA frame number. Still self-contained: it depends on nothing from
 * sdr_dsp.h and operates on raw centred I/Q.
 */

#define GSM_SYMBOL_RATE_HZ (1625000.0 / 6.0) /* ~270833.33 sym/s */
#define GSM_SCH_TRAINING_BITS 64
#define GSM_SCH_INFO_BITS 25
#define GSM_SCH_TAIL_BITS 4
#define GSM_SCH_UNCODED_BITS 39 /* 25 info + 10 parity + 4 tail */
#define GSM_SCH_CODED_BITS 78   /* rate-1/2 convolutional output */
#define GSM_SCH_BURST_BITS 148  /* 3 + 39 + 64 + 39 + 3 */

struct gsm_sch_result {
    int decoded;      /* 1 if a parity-valid SCH was recovered */
    int bsic;         /* 6-bit Base Station Identity Code */
    int ncc;          /* Network Colour Code (BSIC >> 3) */
    int bcc;          /* Base station Colour Code (BSIC & 7) */
    int t1;           /* reduced frame number: T1 (0..2047) */
    int t2;           /* T2 (0..25) */
    int t3;           /* T3 (1,11,21,31,41) */
    int frame_number; /* full TDMA frame number */
    float confidence; /* training-sequence match quality [0,1] */
};

/* The demodulated symbols of the located SCH burst, for a decode visualisation:
   each point is a normalised differential-detection sample (clusters near
   x = +-1 for the two bit decisions), tagged with its hard bit. */
struct gsm_sch_symbols {
    int count;
    float x[GSM_SCH_BURST_BITS];
    float y[GSM_SCH_BURST_BITS];
    uint8_t bit[GSM_SCH_BURST_BITS];
};

/* Channel-encode 25 information bits (MSB first) into 78 coded bits: append the
   10-bit parity and 4 tail bits, then the rate-1/2 K=5 convolutional code. */
void gsm_sch_encode(const uint8_t info_bits[GSM_SCH_INFO_BITS],
                    uint8_t coded_bits[GSM_SCH_CODED_BITS]);

/* Modulate a full SCH burst carrying coded_bits as MSK (GMSK's BT->inf limit)
   into centred I/Q at sample_rate, with the carrier at carrier_offset_hz. Writes
   up to `capacity` pairs starting at output index `start_pair`; returns the
   number of pairs written. Exposed for the deterministic round-trip test. */
size_t gsm_sch_modulate(const uint8_t coded_bits[GSM_SCH_CODED_BITS],
                        double sample_rate, double carrier_offset_hz,
                        size_t start_pair, float *i_out, float *q_out,
                        size_t capacity);

/* Decode the SCH from centred I/Q. carrier_offset_hz is the channel carrier's
   baseband offset (e.g. +400 kHz when tuned to expected-400 kHz). Returns 1 and
   fills result on a parity-valid decode, else 0. When symbols is non-NULL it is
   filled with the located burst's demodulated symbols for visualisation (also
   on a successful decode). */
int gsm_sch_decode(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   double carrier_offset_hz, struct gsm_sch_result *result,
                   struct gsm_sch_symbols *symbols);

#endif
