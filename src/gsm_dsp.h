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

/* How far from its expected place the FCCH tone may be looked for: a bound on
   how far the receiver's own tuning may plausibly be off. 50 kHz is about
   53 ppm at GSM 900 -- generous for an RTL-SDR, and more so once a PPM
   correction has been applied.

   This is a HALF-width, so it must not be raised casually. Search wider and
   the detector will accept whatever narrowband component happens to be the
   most tone-like out there, report high coherence for it, and hand back a
   carrier estimate tens of kHz wrong; nothing about the result says it is
   implausible. At +-100 kHz that cost 14 of 30 blocks on
   testfiles/gsm_arfcn_73.bin. */
#define GSM_FCCH_SEARCH_HALF_HZ 50000.0

struct gsm_fcch_result {
    int detected;
    double tone_frequency_hz; /* baseband; caller maps to the RF carrier */
    float confidence;         /* lag-1 autocorrelation coherence, [0, 1] */
    float amplitude;
};

/* GSM 900 downlink ARFCN (1-124) -> frequency in Hz. Returns 0 for an
   out-of-range ARFCN. */
int gsm_downlink_hz(unsigned int arfcn, uint32_t *frequency_hz);

/* The downlink channel whose carrier is nearest `hz`, or 0 when nothing is
   within half a channel of it. The inverse of the map above, for a caller
   holding a measured frequency and wanting the channel it belongs to. */
int gsm_arfcn_for_hz(double hz);

/* Detect the FCCH pure tone near target_offset_hz (baseband) within
   +/- search_window_hz. Returns 1 (and sets result->detected) when the peak
   coherence meets the internal threshold; result->confidence carries the peak
   coherence found even when it does not. */
int gsm_fcch_detect(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    double target_offset_hz, double search_half_width_hz,
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

    /* Where this burst was found, so the channels that follow it can be found
       too. The BCCH sits in the four TDMA frames after the SCH of frame 1 of
       a 51-multiframe, one timeslot-0 burst every GSM_FRAME_SYMBOLS. */
    int burst_symbol;         /* symbol index of the burst's first symbol */
    double symbol_phase;      /* sub-symbol offset the burst was read at */
    double refined_offset_hz; /* carrier offset the FCCH refined it to */
    int inverted;             /* the demodulator's polarity for this burst */
};

/* One timeslot recurs every eight of them, and a TDMA frame is 156.25 symbols
   per slot: the next burst on this timeslot is this many symbols later. */
#define GSM_FRAME_SYMBOLS 1250

/* A normal burst (GSM 05.02 5.2.3): 3 tail, 57 data, a stealing flag, 26
   training, another flag, 57 data, 3 tail. The training sits in the middle so
   both halves of the data are near a known reference. */
#define GSM_NB_SYMBOLS 148
#define GSM_NB_TAIL 3
#define GSM_NB_DATA_HALF 57
#define GSM_NB_TRAIN_AT 61
#define GSM_BURST_DATA_BITS 114 /* the two data halves together */

/* The eight training sequences a normal burst may carry (GSM 05.02 table
   5.2.3.1). Which one a cell uses is its BCC, the low three bits of the BSIC
   the SCH decodes -- so the SCH is what says where to correlate. */
#define GSM_TSC_COUNT 8
#define GSM_TSC_BITS 26
extern const uint8_t gsm_training_sequences[GSM_TSC_COUNT][GSM_TSC_BITS];

/*
 * Demodulate the normal bursts following a decoded SCH burst, as soft bits.
 *
 * `sch` says where the burst was, at what sub-symbol phase, on which carrier,
 * and through its BCC which training sequence to expect. `soft` receives
 * `count` bursts of GSM_BURST_DATA_BITS values, positive for a 0 bit and
 * negative for a 1, with the magnitude as confidence.
 *
 * Returns how many bursts were demodulated. A burst beyond the end of the
 * block is not one of them, and its slot is left untouched.
 */
int gsm_normal_bursts(const float *i_samples, const float *q_samples,
                      size_t pair_count, double sample_rate,
                      const struct gsm_sch_result *sch, int count,
                      float *soft);

/* The demodulated symbols of the located SCH burst, for a decode visualisation.
   Both the differential-detection product (conj(prev)*cur) and the derotated
   symbol sample (s[k]*e^{-j k pi/2}, a BPSK-like constellation) are provided so
   the UI can toggle representation and amplitude. bit is the per-symbol
   differential decision; chan is the reconstructed channel bit.
   Additional fields capture the correlation landscape, soft symbol magnitudes,
   and unwrapped phase trajectory for the Burst Analysis Chart. */
struct gsm_sch_symbols {
    int count;
    float diff_re[GSM_SCH_BURST_BITS];
    float diff_im[GSM_SCH_BURST_BITS];
    float rot_i[GSM_SCH_BURST_BITS];
    float rot_q[GSM_SCH_BURST_BITS];
    float corr[GSM_SCH_BURST_BITS];     /* correlation landscape around peak */
    float soft_mag[GSM_SCH_BURST_BITS]; /* soft symbol magnitude (|Im|) */
    float phase[GSM_SCH_BURST_BITS];    /* accumulated unwrapped phase */
    uint8_t bit[GSM_SCH_BURST_BITS];
    uint8_t chan[GSM_SCH_BURST_BITS];
};

/* Pack a BSIC and a reduced frame number (T1, T2, T3') into the 25 SCH
   information bits, using the scattered field layout of 3GPP TS 44.018
   10.5.2.1. Exposed so that anything building a burst uses the same bit
   positions the decoder reads, rather than its own inverse of them. */
void gsm_sch_pack_info(int bsic, int t1, int t2, int t3p,
                       uint8_t info_bits[GSM_SCH_INFO_BITS]);

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

#define GSM_OPT_FILTER   1
#define GSM_OPT_FINECFO  2
#define GSM_OPT_TRELLIS  4

/* Decode the SCH from centred I/Q. carrier_offset_hz is the channel carrier's
   baseband offset (e.g. +400 kHz when tuned to expected-400 kHz). Returns 1 and
   fills result on a parity-valid decode, else 0. When symbols is non-NULL it is
   filled with the located burst's demodulated symbols for visualisation (also
   on a successful decode). Options bitmask toggles decode features. */
int gsm_sch_decode(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   double carrier_offset_hz, uint32_t options,
                   struct gsm_sch_result *result,
                   struct gsm_sch_symbols *symbols);

#endif
