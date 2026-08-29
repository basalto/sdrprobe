#include "gsm_dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/* ------------------------------------------------------------------------- */
/* Synchronisation Channel (SCH) decode.                                     */
/* ------------------------------------------------------------------------- */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Extended training sequence of the synchronisation burst (GSM 05.02 5.2.5). */
static const uint8_t sch_training[GSM_SCH_TRAINING_BITS] = {
    1, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1
};

/* 10-bit parity: remainder of d(D)*D^10 mod g(D),
   g(D) = D^10 + D^8 + D^6 + D^5 + D^4 + D^2 + 1. */
static void sch_parity(const uint8_t d[GSM_SCH_INFO_BITS], uint8_t p[10]) {
    unsigned int reg = 0;
    for (int i = 0; i < GSM_SCH_INFO_BITS; i++) {
        unsigned int fb = ((reg >> 9) & 1u) ^ (d[i] & 1u);
        reg = (reg << 1) & 0x3FFu;
        if (fb)
            reg ^= 0x175u; /* D^8+D^6+D^5+D^4+D^2+1 */
    }
    for (int j = 0; j < 10; j++)
        p[j] = (uint8_t)(((reg >> (9 - j)) & 1u) ^ 1u); /* GSM inverts SCH parity */
}

/* Rate-1/2 K=5 convolutional code: G0 = 1+D^3+D^4, G1 = 1+D+D^3+D^4. */
static void sch_conv_encode(const uint8_t u[GSM_SCH_UNCODED_BITS],
                            uint8_t e[GSM_SCH_CODED_BITS]) {
    unsigned int state = 0; /* x[k-1]..x[k-4] in bits 0..3 */
    for (int k = 0; k < GSM_SCH_UNCODED_BITS; k++) {
        unsigned int in = u[k] & 1u;
        unsigned int g0 = in ^ ((state >> 2) & 1u) ^ ((state >> 3) & 1u);
        unsigned int g1 = in ^ (state & 1u) ^ ((state >> 2) & 1u) ^
                          ((state >> 3) & 1u);
        e[2 * k] = (uint8_t)g0;
        e[2 * k + 1] = (uint8_t)g1;
        state = ((state << 1) | in) & 0xFu;
    }
}

void gsm_sch_encode(const uint8_t info_bits[GSM_SCH_INFO_BITS],
                    uint8_t coded_bits[GSM_SCH_CODED_BITS]) {
    uint8_t u[GSM_SCH_UNCODED_BITS];
    uint8_t p[10];
    sch_parity(info_bits, p);
    for (int i = 0; i < GSM_SCH_INFO_BITS; i++)
        u[i] = info_bits[i] & 1u;
    for (int j = 0; j < 10; j++)
        u[GSM_SCH_INFO_BITS + j] = p[j];
    for (int t = 0; t < GSM_SCH_TAIL_BITS; t++)
        u[GSM_SCH_INFO_BITS + 10 + t] = 0;
    sch_conv_encode(u, coded_bits);
}

/* Hard-decision Viterbi decode of the rate-1/2 K=5 code (start and end state 0
   thanks to the 4 tail bits). Recovers the 39 uncoded bits. */
static void sch_viterbi(const uint8_t e[GSM_SCH_CODED_BITS],
                        uint8_t u[GSM_SCH_UNCODED_BITS]) {
    const int states = 16;
    const int steps = GSM_SCH_UNCODED_BITS;
    const int INF = 1 << 20;
    int metric[16];
    int next_metric[16];
    static uint8_t back[GSM_SCH_UNCODED_BITS][16];

    for (int s = 0; s < states; s++)
        metric[s] = (s == 0) ? 0 : INF;

    for (int k = 0; k < steps; k++) {
        int r0 = e[2 * k];
        int r1 = e[2 * k + 1];
        for (int s = 0; s < states; s++)
            next_metric[s] = INF;
        for (int s = 0; s < states; s++) {
            if (metric[s] >= INF)
                continue;
            for (unsigned int in = 0; in < 2; in++) {
                unsigned int g0 = in ^ ((s >> 2) & 1u) ^ ((s >> 3) & 1u);
                unsigned int g1 = in ^ (s & 1u) ^ ((s >> 2) & 1u) ^
                                  ((s >> 3) & 1u);
                int cost = (int)(g0 ^ (unsigned)r0) + (int)(g1 ^ (unsigned)r1);
                int ns = (int)(((s << 1) | in) & 0xF);
                int cand = metric[s] + cost;
                if (cand < next_metric[ns]) {
                    next_metric[ns] = cand;
                    back[k][ns] = (uint8_t)s;
                }
            }
        }
        memcpy(metric, next_metric, sizeof(metric));
    }

    int s = 0; /* known terminating state */
    for (int k = steps - 1; k >= 0; k--) {
        int prev = back[k][s];
        u[k] = (uint8_t)(s & 1u); /* the input bit that produced state s */
        s = prev;
    }
}

/* Verify parity and parse BSIC + reduced frame number from 39 decoded bits. */
static int sch_parse(const uint8_t u[GSM_SCH_UNCODED_BITS],
                     struct gsm_sch_result *result) {
    uint8_t d[GSM_SCH_INFO_BITS];
    uint8_t p_expected[10];
    for (int i = 0; i < GSM_SCH_INFO_BITS; i++)
        d[i] = u[i] & 1u;
    sch_parity(d, p_expected);
    for (int j = 0; j < 10; j++)
        if (p_expected[j] != (u[GSM_SCH_INFO_BITS + j] & 1u))
            return 0;

    int bsic = 0;
    for (int i = 0; i < 6; i++)
        bsic = (bsic << 1) | d[i];
    int t1 = 0;
    for (int i = 6; i < 17; i++)
        t1 = (t1 << 1) | d[i];
    int t2 = 0;
    for (int i = 17; i < 22; i++)
        t2 = (t2 << 1) | d[i];
    int t3p = 0;
    for (int i = 22; i < 25; i++)
        t3p = (t3p << 1) | d[i];
    /* Valid SCH timing: T2 in 0..25, T3' in 0..4 (T3 in {1,11,21,31,41}).
       An out-of-range value means a residual bit error slipped past the 10-bit
       parity, so reject it. */
    if (t2 > 25 || t3p > 4)
        return 0;
    int t3 = 10 * t3p + 1;

    result->bsic = bsic;
    result->ncc = (bsic >> 3) & 7;
    result->bcc = bsic & 7;
    result->t1 = t1;
    result->t2 = t2;
    result->t3 = t3;
    result->frame_number = 51 * (((t3 + 26) - t2) % 26) + t3 + 51 * 26 * t1;
    return 1;
}

size_t gsm_sch_modulate(const uint8_t coded_bits[GSM_SCH_CODED_BITS],
                        double sample_rate, double carrier_offset_hz,
                        size_t start_pair, float *i_out, float *q_out,
                        size_t capacity) {
    uint8_t sym[GSM_SCH_BURST_BITS];
    int idx = 0;
    for (int t = 0; t < 3; t++)
        sym[idx++] = 0;
    for (int k = 0; k < 39; k++)
        sym[idx++] = coded_bits[k];
    for (int k = 0; k < GSM_SCH_TRAINING_BITS; k++)
        sym[idx++] = sch_training[k];
    for (int k = 39; k < 78; k++)
        sym[idx++] = coded_bits[k];
    for (int t = 0; t < 3; t++)
        sym[idx++] = 0;

    /* GSM differentially encodes the burst bits before GMSK modulation: the
       modulating symbol is the XOR of consecutive channel bits. */
    uint8_t de[GSM_SCH_BURST_BITS];
    uint8_t prev = 0;
    for (int n = 0; n < GSM_SCH_BURST_BITS; n++) {
        de[n] = sym[n] ^ prev;
        prev = sym[n];
    }

    double phi[GSM_SCH_BURST_BITS + 1];
    phi[0] = 0.0;
    for (int n = 0; n < GSM_SCH_BURST_BITS; n++) {
        double a = 1.0 - 2.0 * de[n];
        phi[n + 1] = phi[n] + a * (M_PI / 2.0);
    }

    const double amplitude = 50.0;
    double sps = sample_rate / GSM_SYMBOL_RATE_HZ;
    size_t nsamp = (size_t)((double)GSM_SCH_BURST_BITS * sps);
    size_t written = 0;
    for (size_t k = 0; k < nsamp; k++) {
        size_t out = start_pair + k;
        if (out >= capacity)
            break;
        double p = (double)k / sps;
        int n = (int)floor(p);
        double frac = p - n;
        double a = (n < GSM_SCH_BURST_BITS) ? (1.0 - 2.0 * de[n]) : 0.0;
        double baseband = phi[n < GSM_SCH_BURST_BITS ? n : GSM_SCH_BURST_BITS] +
                          frac * a * (M_PI / 2.0);
        double carrier = GSM_TWO_PI * carrier_offset_hz * (double)k / sample_rate;
        double total = baseband + carrier;
        i_out[out] = (float)(amplitude * cos(total));
        q_out[out] = (float)(amplitude * sin(total));
        written++;
    }
    return written;
}

#define SCH_TIMINGS 8
#define SCH_MIN_MATCH 0.80f

/* Interpolate a downconverted sample at fractional index t. */
static void sch_interp(const float *bi, const float *bq, size_t count, double t,
                       double *ri, double *rq) {
    if (t < 0.0)
        t = 0.0;
    size_t n = (size_t)t;
    if (n + 1 >= count) {
        *ri = bi[count - 1];
        *rq = bq[count - 1];
        return;
    }
    double fr = t - (double)n;
    *ri = bi[n] * (1.0 - fr) + bi[n + 1] * fr;
    *rq = bq[n] * (1.0 - fr) + bq[n + 1] * fr;
}

/* Differential-demodulate the downconverted baseband into per-symbol bits
   m[k] = 1 when the phase retards (GSM's convention with this sign): these are
   the differentially-encoded channel bits, m[k] = d[k] ^ d[k-1]. */
static void sch_diff_bits(const float *bi, const float *bq, size_t pair_count,
                          double sps, double phase0, int nsym, uint8_t *m) {
    double prev_i, prev_q;
    sch_interp(bi, bq, pair_count, phase0, &prev_i, &prev_q);
    m[0] = 0;
    for (int k = 1; k < nsym; k++) {
        double si, sq;
        sch_interp(bi, bq, pair_count, phase0 + (double)k * sps, &si, &sq);
        double im = prev_i * sq - prev_q * si; /* Im(conj(prev)*cur) */
        m[k] = (im < 0.0) ? 1u : 0u;
        prev_i = si;
        prev_q = sq;
    }
}

int gsm_sch_decode(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   double carrier_offset_hz, struct gsm_sch_result *result,
                   struct gsm_sch_symbols *symbols) {
    if (!i_samples || !q_samples || !result)
        return 0;
    if (symbols)
        symbols->count = 0;
    double sps = sample_rate / GSM_SYMBOL_RATE_HZ;
    if (pair_count < (size_t)(2.0 * GSM_SCH_BURST_BITS * sps))
        return 0;

    memset(result, 0, sizeof(*result));

    /* Refine the carrier from the FCCH tone. An uncalibrated receiver can be
       tens of kHz off, which biases the differential demod enough to break the
       decode; the FCCH is a clean reference at carrier + 1625/24 kHz. */
    double refined = carrier_offset_hz;
    struct gsm_fcch_result fcch;
    if (gsm_fcch_detect(i_samples, q_samples, pair_count, sample_rate,
                        carrier_offset_hz + GSM_FCCH_TONE_HZ, 100000.0, &fcch))
        refined = fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;

    float *bi = malloc(pair_count * sizeof(*bi));
    float *bq = malloc(pair_count * sizeof(*bq));
    if (!bi || !bq) {
        free(bi);
        free(bq);
        return 0;
    }
    for (size_t n = 0; n < pair_count; n++) {
        double ph = -GSM_TWO_PI * refined * (double)n / sample_rate;
        double c = cos(ph), s = sin(ph);
        bi[n] = (float)(i_samples[n] * c - q_samples[n] * s);
        bq[n] = (float)(i_samples[n] * s + q_samples[n] * c);
    }

    int nsym = (int)(((double)pair_count - 2.0) / sps) - 1;
    if (nsym < GSM_SCH_BURST_BITS + 4) {
        free(bi);
        free(bq);
        return 0;
    }
    uint8_t *m = malloc((size_t)nsym * sizeof(*m));
    if (!m) {
        free(bi);
        free(bq);
        return 0;
    }

    /* The differentially-encoded training pattern (well defined for j>=1). */
    uint8_t train_diff[GSM_SCH_TRAINING_BITS];
    for (int j = 1; j < GSM_SCH_TRAINING_BITS; j++)
        train_diff[j] = sch_training[j] ^ sch_training[j - 1];

    int best_match = 0, best_timing = -1, best_pos = -1, best_invert = 0;
    for (int t = 0; t < SCH_TIMINGS; t++) {
        double phase0 = (double)t * sps / (double)SCH_TIMINGS;
        sch_diff_bits(bi, bq, pair_count, sps, phase0, nsym, m);
        int last = nsym - GSM_SCH_TRAINING_BITS - 39;
        for (int p = 39; p < last; p++) {
            int matches = 0;
            for (int j = 1; j < GSM_SCH_TRAINING_BITS; j++)
                matches += (m[p + j] == train_diff[j]);
            int invert = 0;
            int score = matches;
            if ((GSM_SCH_TRAINING_BITS - 1) - matches > matches) {
                score = (GSM_SCH_TRAINING_BITS - 1) - matches;
                invert = 1;
            }
            if (score > best_match) {
                best_match = score;
                best_timing = t;
                best_pos = p;
                best_invert = invert;
            }
        }
    }

    float best_ratio = (float)best_match / (float)(GSM_SCH_TRAINING_BITS - 1);
    int decoded = 0;
    if (best_ratio >= SCH_MIN_MATCH && best_timing >= 0) {
        double phase0 = (double)best_timing * sps / (double)SCH_TIMINGS;
        sch_diff_bits(bi, bq, pair_count, sps, phase0, nsym, m);
        if (best_invert)
            for (int k = 0; k < nsym; k++)
                m[k] ^= 1u;

        /* Reconstruct the channel bits anchored at the known training sequence
           (d[p+j] = TRAIN[j]), so a bit error in a data field can only corrupt
           that field toward the anchor and the parity will reject it, rather
           than a global polarity flip hiding the error. */
        int p = best_pos; /* d[p] = TRAIN[0] */
        uint8_t coded[GSM_SCH_CODED_BITS];
        /* data2 forward from d[p+63] = TRAIN[63]: d[k] = d[k-1] ^ m[k]. */
        uint8_t prev = sch_training[GSM_SCH_TRAINING_BITS - 1];
        int base2 = p + GSM_SCH_TRAINING_BITS;
        for (int j = 0; j < 39; j++) {
            uint8_t bit = prev ^ m[base2 + j];
            coded[39 + j] = bit;
            prev = bit;
        }
        /* data1 backward from d[p] = TRAIN[0]: d[k-1] = d[k] ^ m[k]. */
        prev = sch_training[0];
        for (int i = 0; i < 39; i++) {
            uint8_t bit = prev ^ m[p - i];
            coded[38 - i] = bit;
            prev = bit;
        }

        uint8_t u[GSM_SCH_UNCODED_BITS];
        sch_viterbi(coded, u);
        if (sch_parse(u, result)) {
            result->decoded = 1;
            result->confidence = best_ratio;
            decoded = 1;

            /* Capture the burst's symbols for a decode constellation: both the
               differential-detection product and the derotated sample. */
            if (symbols) {
                int first = best_pos - 42; /* burst start (3 tail bits before) */
                if (first < 1)
                    first = 1;
                double prev_i, prev_q;
                sch_interp(bi, bq, pair_count, phase0 + (double)(first - 1) * sps,
                           &prev_i, &prev_q);
                int count = 0;
                uint8_t chan_prev = 0;
                for (int k = first;
                     k < first + GSM_SCH_BURST_BITS && k < nsym; k++) {
                    double si, sq;
                    sch_interp(bi, bq, pair_count, phase0 + (double)k * sps, &si,
                               &sq);
                    double re = prev_i * si + prev_q * sq;
                    double im = prev_i * sq - prev_q * si;
                    symbols->diff_re[count] = (float)re;
                    symbols->diff_im[count] = (float)im;
                    /* Derotate by e^{-j k pi/2} -> BPSK-like on the real axis. */
                    double a = -GSM_TWO_PI * (double)k / 4.0;
                    double cr = cos(a), sr = sin(a);
                    symbols->rot_i[count] = (float)(si * cr - sq * sr);
                    symbols->rot_q[count] = (float)(si * sr + sq * cr);
                    uint8_t b = (im < 0.0) ? 1u : 0u;
                    symbols->bit[count] = b;
                    if (best_invert)
                        b ^= 1u;
                    chan_prev ^= b;
                    symbols->chan[count] = chan_prev;
                    count++;
                    prev_i = si;
                    prev_q = sq;
                }
                symbols->count = count;
            }
        }
    }

    free(bi);
    free(bq);
    free(m);
    return decoded;
}
