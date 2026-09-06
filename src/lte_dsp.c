#include "lte_dsp.h"

#include "lte_gold.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The two gates, and why the second one carries the weight.
 *
 * A PSS correlation is a maximum over 28800 alignments -- 9600 sample offsets
 * against three roots -- so noise alone reaches further than intuition
 * suggests. Measured over a hundred blocks of pure noise the peak got to
 * 0.353, while a cell at 16 dB never fell below 0.723. A threshold anywhere
 * between them would separate those two populations, but a weak cell off a
 * rooftop antenna is not the 16 dB case, so this one is set low on purpose:
 * passing it means a candidate worth taking to the secondary sequence, not a
 * cell. Nothing is claimed on the strength of it alone.
 */
#define LTE_PSS_MIN_CORRELATION 0.30f

/*
 * The secondary sequence is where a cell is actually claimed, and it is a far
 * harder test to pass by accident: 336 candidate sequences, and the winner has
 * to both match well and beat the 335 others.
 *
 * The margin does nearly all the work, which is worth knowing before either
 * number is touched. Measured over blocks with no LTE cell in them -- forty of
 * pure noise, and the GSM and Mode S captures in testfiles/, which are real
 * structured signals and score higher than noise does -- the score itself
 * reached 0.565 but the margin never exceeded 0.067. Over the three live band
 * 20 captures the score ran 0.45 to 0.78 and the margin 0.16 to 0.35 on the
 * blocks that carried a readable cell. A gate on the score alone would have to
 * sit above 0.57 and would throw away half the real ones; a gate on the margin
 * separates the two populations with room to spare.
 */
#define LTE_SSS_MIN_CORRELATION 0.45f
#define LTE_SSS_MIN_MARGIN 0.12f


/* ------------------------------------------------------------------ */
/* A 128-point FFT, hand-written and self-contained (ADR-0003).        */
/* ------------------------------------------------------------------ */

static void fft128(float *re, float *im, int inverse) {
    const unsigned int n = LTE_FFT_SIZE;
    unsigned int i, j, len;

    for (i = 1, j = 0; i < n; i++) {
        unsigned int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double step = 2.0 * M_PI / (double)len * (inverse ? 1.0 : -1.0);
        unsigned int half = len >> 1;
        for (i = 0; i < n; i += len) {
            for (j = 0; j < half; j++) {
                double angle = step * (double)j;
                float wr = (float)cos(angle);
                float wi = (float)sin(angle);
                float ur = re[i + j], ui = im[i + j];
                float vr = re[i + j + half] * wr - im[i + j + half] * wi;
                float vi = re[i + j + half] * wi + im[i + j + half] * wr;
                re[i + j] = ur + vr;
                im[i + j] = ui + vi;
                re[i + j + half] = ur - vr;
                im[i + j + half] = ui - vi;
            }
        }
    }
    if (inverse) {
        for (i = 0; i < n; i++) {
            re[i] /= (float)n;
            im[i] /= (float)n;
        }
    }
}

int lte_subcarrier_bin(int subcarrier) {
    if (subcarrier >= 0)
        return subcarrier % LTE_FFT_SIZE;
    return LTE_FFT_SIZE + (subcarrier % LTE_FFT_SIZE);
}

void lte_symbol_fft(const float *i_samples, const float *q_samples,
                    float *real_out, float *imag_out) {
    int n;
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        real_out[n] = i_samples[n];
        imag_out[n] = q_samples[n];
    }
    fft128(real_out, imag_out, 0);
}

/* The same, with a frequency offset taken out first. The rotation is measured
   from `origin` rather than from the start of the symbol, so two symbols of
   the same block come out with a consistent phase between them -- which is
   what lets SSS be detected against PSS, and the reference signals of one
   symbol equalise the next. */
static void symbol_fft_corrected(const float *i_samples, const float *q_samples,
                                 size_t start, size_t origin,
                                 double offset_hz, double sample_rate,
                                 float *real_out, float *imag_out) {
    double step = -2.0 * M_PI * offset_hz / sample_rate;
    int n;
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        double phase = step * (double)(start + (size_t)n - origin);
        float cr = (float)cos(phase);
        float ci = (float)sin(phase);
        float ir = i_samples[start + (size_t)n];
        float qi = q_samples[start + (size_t)n];
        real_out[n] = ir * cr - qi * ci;
        imag_out[n] = ir * ci + qi * cr;
    }
    fft128(real_out, imag_out, 0);
}


/* ------------------------------------------------------------------ */
/* EARFCN <-> frequency.                                              */
/* ------------------------------------------------------------------ */

/*
 * FDD downlink allocations, lowest EARFCN first and non-overlapping, as
 * 36.101 Table 5.7.3-1 numbers them. Bands 1, 3 and 7 are here although an
 * R820T tuner tops out below them: a caller holding an EARFCN deserves the
 * frequency it names, and whether the receiver can hear it is a separate
 * question the band plan and the tuner answer.
 *
 * Band 28 is cut short here, and deliberately. 3GPP runs it to EARFCN 27659,
 * 803 MHz, which overlaps band 20 from 791 MHz up -- one frequency, two
 * channel numbers, and no way for a reverse lookup to say which was meant.
 * Europe never allocated that overlap: the APT700 arrangement stops the
 * downlink at 788 MHz, which is EARFCN 27509, and that is what band_plan.c
 * records for Portugal too. The channels above it are absent rather than
 * wrong, the same position the band plan takes.
 */
static const struct lte_band bands[] = {
    {  1,     0,   599, 2110000000.0, "2100 MHz" },
    {  3,  1200,  1949, 1805000000.0, "1800 MHz" },
    {  7,  2750,  3449, 2620000000.0, "2600 MHz" },
    {  8,  3450,  3799,  925000000.0, "900 MHz" },
    { 20,  6150,  6449,  791000000.0, "800 MHz" },
    { 28, 27210, 27509,  758000000.0, "700 MHz" }
};

int lte_band_count(void) {
    return (int)(sizeof(bands) / sizeof(bands[0]));
}

const struct lte_band *lte_band_for_number(int number) {
    int i;
    for (i = 0; i < lte_band_count(); i++)
        if (lte_band_at(i)->band == number)
            return lte_band_at(i);
    return NULL;
}

int lte_reachable_band(int index) {
    static const int reachable[LTE_REACHABLE_BANDS] = { 28, 20, 8 };
    if (index < 0 || index >= LTE_REACHABLE_BANDS)
        return 0;
    return reachable[index];
}

const struct lte_band *lte_band_at(int index) {
    if (index < 0 || index >= lte_band_count())
        return NULL;
    return &bands[index];
}

const struct lte_band *lte_band_for_earfcn(unsigned int earfcn) {
    int i;
    for (i = 0; i < lte_band_count(); i++)
        if (earfcn >= bands[i].earfcn_low && earfcn <= bands[i].earfcn_high)
            return &bands[i];
    return NULL;
}

int lte_earfcn_downlink_hz(unsigned int earfcn, uint32_t *frequency_hz) {
    const struct lte_band *band = lte_band_for_earfcn(earfcn);
    if (!band || !frequency_hz)
        return 0;
    *frequency_hz = (uint32_t)(band->downlink_low_hz +
                               100000.0 * (double)(earfcn - band->earfcn_low));
    return 1;
}

int lte_earfcn_for_hz(double hz) {
    int i;
    for (i = 0; i < lte_band_count(); i++) {
        const struct lte_band *band = &bands[i];
        double span = 100000.0 * (double)(band->earfcn_high - band->earfcn_low);
        double steps;
        long index;
        if (hz < band->downlink_low_hz - 50000.0 ||
            hz > band->downlink_low_hz + span + 50000.0)
            continue;
        steps = (hz - band->downlink_low_hz) / 100000.0;
        index = (long)floor(steps + 0.5);
        if (index < 0)
            index = 0;
        if (index > (long)(band->earfcn_high - band->earfcn_low))
            index = (long)(band->earfcn_high - band->earfcn_low);
        return (int)(band->earfcn_low + (unsigned int)index);
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* The synchronisation sequences.                                     */
/* ------------------------------------------------------------------ */

/*
 * Zadoff-Chu roots for N_ID_2 = 0, 1, 2 (36.211 section 6.11.1.1).
 *
 * The sign of the exponent below is not a matter of taste, and getting it
 * wrong is close to invisible. Conjugating a Zadoff-Chu sequence of this
 * length turns root u into root 63 - u, and 63 - 29 = 34: roots 29 and 34 are
 * each other's conjugate. A detector built on the conjugated sequences
 * therefore still finds every cell, still reports a sharp correlation and a
 * coherent channel -- and quietly swaps N_ID_2 1 with N_ID_2 2, while making
 * N_ID_2 0 invisible, because 63 - 25 = 38 is not a root any cell uses.
 *
 * A synthetic round trip cannot catch that: the generator and the detector
 * conjugate together and agree perfectly.
 *
 * The sign below is NEGATIVE, and that is not a judgement call -- it is
 * 36.211 and it is srsRAN's lib/src/phy/sync/pss.c, which reads
 *
 *     const float root_value[] = {25.0, 29.0, 34.0};
 *     root_idx = N_id_2;
 *     int sign = -1;
 *     arg = sign * M_PI * root_value[root_idx] * (i * (i + 1)) / 63.0;
 *
 * It has been flipped once already, on the strength of the secondary sequence
 * disagreeing with it, and that was the wrong end to move: the disagreement
 * was real but the secondary detector was the one at fault. Do not flip it
 * again to make something downstream agree. See
 * .scratch/lte-cell-search/issues/04-the-conjugated-primary-sequence.md.
 */
static const int pss_roots[LTE_N_ID_2_COUNT] = { 25, 29, 34 };

void lte_pss_sequence(int n_id_2, float *real, float *imag) {
    int u, n;
    if (n_id_2 < 0 || n_id_2 >= LTE_N_ID_2_COUNT)
        return;
    u = pss_roots[n_id_2];
    for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++) {
        /* The length-63 sequence with its middle element punctured, which is
           what the change of argument at n = 31 amounts to: the element that
           would land on the unused DC subcarrier is skipped rather than
           moved. */
        double m = (n < 31) ? (double)n * (double)(n + 1)
                            : (double)(n + 1) * (double)(n + 2);
        double phase = -M_PI * (double)u * m / 63.0;
        real[n] = (float)cos(phase);
        imag[n] = (float)sin(phase);
    }
}

/* The physical subcarrier a synchronisation-sequence element sits on: 31
   either side of DC, which is skipped. */
static int sync_subcarrier(int n) {
    return (n < 31) ? n - 31 : n - 30;
}

/* The three length-31 m-sequences of 36.211 section 6.11.2.1, as +-1. */
static void sss_base_sequences(int *s, int *c, int *z) {
    int x[31];
    int i;

    x[0] = 0; x[1] = 0; x[2] = 0; x[3] = 0; x[4] = 1;
    for (i = 0; i < 26; i++)
        x[i + 5] = (x[i + 2] + x[i]) % 2;
    for (i = 0; i < 31; i++)
        s[i] = 1 - 2 * x[i];

    x[0] = 0; x[1] = 0; x[2] = 0; x[3] = 0; x[4] = 1;
    for (i = 0; i < 26; i++)
        x[i + 5] = (x[i + 3] + x[i]) % 2;
    for (i = 0; i < 31; i++)
        c[i] = 1 - 2 * x[i];

    x[0] = 0; x[1] = 0; x[2] = 0; x[3] = 0; x[4] = 1;
    for (i = 0; i < 26; i++)
        x[i + 5] = (x[i + 4] + x[i + 2] + x[i + 1] + x[i]) % 2;
    for (i = 0; i < 31; i++)
        z[i] = 1 - 2 * x[i];
}

/* N_ID_1 -> the two cyclic shifts, the closed form of 36.211 table
   6.11.2.1-1. */
static void sss_shifts(int n_id_1, int *m0, int *m1) {
    int q_prime = n_id_1 / 30;
    int q = (n_id_1 + q_prime * (q_prime + 1) / 2) / 30;
    int m_prime = n_id_1 + q * (q + 1) / 2;
    *m0 = m_prime % 31;
    *m1 = (*m0 + m_prime / 31 + 1) % 31;
}

void lte_sss_sequence(int n_id_1, int n_id_2, int subframe5, float *values) {
    int s[31], c[31], z[31];
    int m0, m1, n;

    if (n_id_1 < 0 || n_id_1 >= LTE_N_ID_1_COUNT ||
        n_id_2 < 0 || n_id_2 >= LTE_N_ID_2_COUNT)
        return;

    sss_base_sequences(s, c, z);
    sss_shifts(n_id_1, &m0, &m1);

    for (n = 0; n < 31; n++) {
        int s0 = s[(n + m0) % 31];
        int s1 = s[(n + m1) % 31];
        int c0 = c[(n + n_id_2) % 31];
        int c1 = c[(n + n_id_2 + 3) % 31];
        int z0 = z[(n + (m0 % 8)) % 31];
        int z1 = z[(n + (m1 % 8)) % 31];
        if (!subframe5) {
            values[2 * n] = (float)(s0 * c0);
            values[2 * n + 1] = (float)(s1 * c1 * z0);
        } else {
            values[2 * n] = (float)(s1 * c0);
            values[2 * n + 1] = (float)(s0 * c1 * z1);
        }
    }
}


/* ------------------------------------------------------------------ */
/* PSS detection.                                                     */
/* ------------------------------------------------------------------ */

struct pss_reference {
    float re[LTE_FFT_SIZE];
    float im[LTE_FFT_SIZE];
    double half_energy[2];
};

/* The time-domain PSS: the sequence laid on its subcarriers and transformed
   back. This is the waveform a cell actually radiates in that symbol, so
   correlating against it is correlating against the transmission rather than
   against an idea of it. */
static void pss_reference_build(int n_id_2, struct pss_reference *reference) {
    float grid_re[LTE_FFT_SIZE], grid_im[LTE_FFT_SIZE];
    float seq_re[LTE_SYNC_SUBCARRIERS], seq_im[LTE_SYNC_SUBCARRIERS];
    int n;

    memset(grid_re, 0, sizeof(grid_re));
    memset(grid_im, 0, sizeof(grid_im));
    lte_pss_sequence(n_id_2, seq_re, seq_im);
    for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++) {
        int bin = lte_subcarrier_bin(sync_subcarrier(n));
        grid_re[bin] = seq_re[n];
        grid_im[bin] = seq_im[n];
    }
    fft128(grid_re, grid_im, 1);

    reference->half_energy[0] = 0.0;
    reference->half_energy[1] = 0.0;
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        reference->re[n] = grid_re[n];
        reference->im[n] = grid_im[n];
        reference->half_energy[n < LTE_FFT_SIZE / 2 ? 0 : 1] +=
            (double)grid_re[n] * grid_re[n] + (double)grid_im[n] * grid_im[n];
    }
}

/* The correlation of one root at one alignment, normalised the way the search
   normalises it. Used by the search's inner loop and, afterwards, to walk a
   window either side of the peak for the trace. */
static float pss_score_at(const float *i_samples, const float *q_samples,
                          size_t start, const struct pss_reference *ref) {
    double magnitude[2];
    int half, n;
    for (half = 0; half < 2; half++) {
        double cr = 0.0, ci = 0.0, energy = 0.0;
        int from = half * (LTE_FFT_SIZE / 2);
        int to = from + LTE_FFT_SIZE / 2;
        for (n = from; n < to; n++) {
            float xr = i_samples[start + (size_t)n];
            float xi = q_samples[start + (size_t)n];
            cr += (double)xr * ref->re[n] + (double)xi * ref->im[n];
            ci += (double)xi * ref->re[n] - (double)xr * ref->im[n];
            energy += (double)xr * xr + (double)xi * xi;
        }
        if (energy <= 0.0)
            return 0.0f;
        magnitude[half] = sqrt(cr * cr + ci * ci) /
                          sqrt(energy * ref->half_energy[half]);
    }
    return (float)(0.5 * (magnitude[0] + magnitude[1]));
}

int lte_pss_detect(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   struct lte_pss_result *result, struct lte_trace *trace) {
    struct pss_reference reference[LTE_N_ID_2_COUNT];
    size_t span, start;
    double window_energy[2];
    float best_by_root[LTE_N_ID_2_COUNT];
    float best = -1.0f, runner_up = 0.0f;
    int best_root = -1;
    size_t best_start = 0;
    double best_offset = 0.0;
    int r;

    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    if (!i_samples || !q_samples || pair_count < LTE_FFT_SIZE)
        return -1;
    /* The whole file's arithmetic is LTE's grid; a stream on another one is
       refused rather than quietly resampled. */
    if (fabs(sample_rate - LTE_SAMPLE_RATE_HZ) > 1.0)
        return -1;

    for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
        pss_reference_build(r, &reference[r]);
        best_by_root[r] = -1.0f;
    }

    /* One half-frame is one PSS: searching further finds the same cell. */
    span = pair_count - LTE_FFT_SIZE + 1;
    if (span > LTE_HALF_FRAME_SAMPLES)
        span = LTE_HALF_FRAME_SAMPLES;

    window_energy[0] = 0.0;
    window_energy[1] = 0.0;
    for (start = 0; start < LTE_FFT_SIZE; start++) {
        double e = (double)i_samples[start] * i_samples[start] +
                   (double)q_samples[start] * q_samples[start];
        window_energy[start < LTE_FFT_SIZE / 2 ? 0 : 1] += e;
    }

    for (start = 0; start < span; start++) {
        if (start > 0) {
            /* Slide both halves by one sample. */
            size_t leaving = start - 1;
            size_t crossing = start - 1 + LTE_FFT_SIZE / 2;
            size_t entering = start - 1 + LTE_FFT_SIZE;
            double out = (double)i_samples[leaving] * i_samples[leaving] +
                         (double)q_samples[leaving] * q_samples[leaving];
            double mid = (double)i_samples[crossing] * i_samples[crossing] +
                         (double)q_samples[crossing] * q_samples[crossing];
            double in = (double)i_samples[entering] * i_samples[entering] +
                        (double)q_samples[entering] * q_samples[entering];
            window_energy[0] += mid - out;
            window_energy[1] += in - mid;
        }
        if (window_energy[0] <= 0.0 || window_energy[1] <= 0.0)
            continue;

        for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
            const struct pss_reference *ref = &reference[r];
            double c_re[2] = { 0.0, 0.0 }, c_im[2] = { 0.0, 0.0 };
            double magnitude[2];
            float score;
            int half, n;

            for (half = 0; half < 2; half++) {
                int from = half * (LTE_FFT_SIZE / 2);
                int to = from + LTE_FFT_SIZE / 2;
                for (n = from; n < to; n++) {
                    float xr = i_samples[start + (size_t)n];
                    float xi = q_samples[start + (size_t)n];
                    /* x * conj(reference) */
                    c_re[half] += (double)xr * ref->re[n] +
                                  (double)xi * ref->im[n];
                    c_im[half] += (double)xi * ref->re[n] -
                                  (double)xr * ref->im[n];
                }
                magnitude[half] = sqrt(c_re[half] * c_re[half] +
                                       c_im[half] * c_im[half]) /
                                  sqrt(window_energy[half] *
                                       ref->half_energy[half]);
            }
            score = (float)(0.5 * (magnitude[0] + magnitude[1]));

            if (score > best_by_root[r])
                best_by_root[r] = score;
            if (score > best) {
                best = score;
                best_root = r;
                best_start = start;
                /* The phase the second half has turned relative to the first
                   is 64 samples of the frequency error. Half a turn is half a
                   subcarrier, so this reads +-7.5 kHz and no further. */
                best_offset = atan2(c_im[1] * c_re[0] - c_re[1] * c_im[0],
                                    c_re[1] * c_re[0] + c_im[1] * c_im[0]) *
                              sample_rate /
                              (2.0 * M_PI * (LTE_FFT_SIZE / 2.0));
            }
        }
    }

    if (best_root < 0)
        return 0;

    /* What the other two roots managed at their own best alignment. A cell
       beats them by a wide margin; noise does not. */
    for (r = 0; r < LTE_N_ID_2_COUNT; r++)
        if (r != best_root && best_by_root[r] > runner_up)
            runner_up = best_by_root[r];

    result->n_id_2 = best_root;
    result->useful_start = best_start;
    result->peak = best;
    result->runner_up = runner_up;
    result->frequency_offset_hz = best_offset;
    result->detected = best >= LTE_PSS_MIN_CORRELATION;

    /*
     * A second, tiny pass either side of the peak, for anyone drawing it.
     * Separate from the search on purpose: the search runs 28800 alignments
     * and has no business carrying a buffer through them, and this is 193.
     */
    if (trace) {
        long first = (long)best_start - LTE_TRACE_PROFILE / 2;
        int n;
        trace->profile_count = 0;
        trace->profile_peak = 0;
        for (n = 0; n < LTE_TRACE_PROFILE; n++) {
            long at = first + n;
            if (at < 0 || (size_t)at + LTE_FFT_SIZE > pair_count) {
                trace->profile[n] = 0.0f;
                continue;
            }
            trace->profile[n] = pss_score_at(i_samples, q_samples, (size_t)at,
                                             &reference[best_root]);
            if ((size_t)at == best_start)
                trace->profile_peak = n;
        }
        trace->profile_count = LTE_TRACE_PROFILE;
    }
    return result->detected;
}


/* ------------------------------------------------------------------ */
/* SSS, and the cell the two of them describe.                        */
/* ------------------------------------------------------------------ */

/*
 * How many frames of the secondary sequence to add up. A sample block covers
 * about seven, and the sequence is the same in every one of them.
 */
#define LTE_SSS_MAX_FRAMES 8
#define LTE_SSS_PAIRS (LTE_SYNC_SUBCARRIERS - 1)

/* One frame's differential reading of the secondary-sequence symbol. */
struct sss_observation {
    float re[LTE_SSS_PAIRS];
    float im[LTE_SSS_PAIRS];
};

/*
 * Read the symbol differentially: each subcarrier times the conjugate of its
 * neighbour.
 *
 * The channel cancels because two neighbouring subcarriers went through
 * almost the same one, and so does any error in where the symbol's window was
 * placed -- a timing error is a phase ramp across the subcarriers, and
 * neighbouring points on a ramp differ by a constant that the whole
 * correlation absorbs. That is the property this file needs and the reason
 * the obvious alternative was abandoned: dividing the secondary sequence by a
 * channel measured from the primary one works on a signal built to be perfect
 * and fails on live air, where the two symbols are not sampled at quite the
 * same point of their own symbols. Live captures that scored 0.44 that way --
 * indistinguishable from noise -- score 0.75 this way.
 *
 * The cost is that a sequence and its negation read alike. No two candidates
 * are negations of each other, so nothing is lost.
 */
static void sss_observe(const float *i_samples, const float *q_samples,
                        size_t start, double offset_hz, double sample_rate,
                        struct sss_observation *observation) {
    float re[LTE_FFT_SIZE], im[LTE_FFT_SIZE];
    int n;

    symbol_fft_corrected(i_samples, q_samples, start, 0, offset_hz,
                         sample_rate, re, im);
    for (n = 0; n < LTE_SSS_PAIRS; n++) {
        int here = lte_subcarrier_bin(sync_subcarrier(n));
        int next = lte_subcarrier_bin(sync_subcarrier(n + 1));
        observation->re[n] = re[here] * re[next] + im[here] * im[next];
        observation->im[n] = im[here] * re[next] - re[here] * im[next];
    }
}

/* The 62 elements a candidate identity and half-frame would have. */
static void sss_candidate(int n_id_1, int n_id_2, int subframe5,
                          const int *s, const int *c, const int *z,
                          int *values) {
    int m0, m1, n;
    sss_shifts(n_id_1, &m0, &m1);
    for (n = 0; n < 31; n++) {
        int s0 = s[(n + m0) % 31];
        int s1 = s[(n + m1) % 31];
        int c0 = c[(n + n_id_2) % 31];
        int c1 = c[(n + n_id_2 + 3) % 31];
        if (!subframe5) {
            values[2 * n] = s0 * c0;
            values[2 * n + 1] = s1 * c1 * z[(n + (m0 % 8)) % 31];
        } else {
            values[2 * n] = s1 * c0;
            values[2 * n + 1] = s0 * c1 * z[(n + (m1 % 8)) % 31];
        }
    }
}

/*
 * Score every N_ID_1 and both half-frames against however many frames were
 * read, and return the best.
 *
 * The frames are added by magnitude rather than coherently: the same sequence
 * arrives in each, but ten milliseconds apart the channel and whatever
 * frequency error is left have turned it by an unknown amount, and adding
 * those directly would cancel as often as it reinforced.
 */
static int sss_best_candidate(const struct sss_observation *frames, int count,
                              int n_id_2, int *subframe5, float *best_score,
                              float *runner_up_score, float *scores) {
    int s[31], c[31], z[31];
    double magnitude = 0.0;
    float best = -1.0f, second = -1.0f;
    int best_id = 0, best_half = 0;
    int n_id_1, k, n;

    sss_base_sequences(s, c, z);
    for (k = 0; k < count; k++)
        for (n = 0; n < LTE_SSS_PAIRS; n++)
            magnitude += sqrt((double)frames[k].re[n] * frames[k].re[n] +
                              (double)frames[k].im[n] * frames[k].im[n]);
    if (magnitude <= 0.0 || count <= 0)
        return -1;

    for (n_id_1 = 0; n_id_1 < LTE_N_ID_1_COUNT; n_id_1++) {
        int half;
        for (half = 0; half < 2; half++) {
            int values[LTE_SYNC_SUBCARRIERS];
            double total = 0.0;
            float score;
            sss_candidate(n_id_1, n_id_2, half, s, c, z, values);
            for (k = 0; k < count; k++) {
                double sum_re = 0.0, sum_im = 0.0;
                for (n = 0; n < LTE_SSS_PAIRS; n++) {
                    int weight = values[n] * values[n + 1];
                    sum_re += (double)frames[k].re[n] * weight;
                    sum_im += (double)frames[k].im[n] * weight;
                }
                total += sqrt(sum_re * sum_re + sum_im * sum_im);
            }
            score = (float)(total / magnitude);
            /* The chart wants one line, not two interleaved, so it gets the
               better of the two half-frames at each identity. */
            if (scores && (half == 0 || score > scores[n_id_1]))
                scores[n_id_1] = score;
            if (score > best) {
                second = best;
                best = score;
                best_id = n_id_1;
                best_half = half;
            } else if (score > second) {
                second = score;
            }
        }
    }

    if (subframe5)
        *subframe5 = best_half;
    if (best_score)
        *best_score = best;
    if (runner_up_score)
        *runner_up_score = second < 0.0f ? 0.0f : second;
    return best_id;
}

/*
 * One cyclic-prefix hypothesis: take the secondary-sequence symbol where that
 * prefix puts it, in every frame the block holds, and see how well the sum
 * matches a candidate. Which hypothesis matches better is the only measurement
 * in the chain that tells the two prefixes apart -- they put the symbol 23
 * samples apart, far enough that the wrong one lands outside the symbol
 * altogether.
 */
static int sss_try(const float *i_samples, const float *q_samples,
                   size_t pair_count, const struct lte_pss_result *pss,
                   double sample_rate, double offset_hz, int lead,
                   int *n_id_1, int *subframe5, float *score,
                   float *runner_up, float *scores) {
    struct sss_observation frames[LTE_SSS_MAX_FRAMES];
    int count = 0, found;

    if (pss->useful_start < (size_t)lead)
        return -1;
    while (count < LTE_SSS_MAX_FRAMES) {
        size_t start = pss->useful_start - (size_t)lead +
                       (size_t)count * LTE_FRAME_SAMPLES;
        if (start + LTE_FFT_SIZE > pair_count)
            break;
        sss_observe(i_samples, q_samples, start, offset_hz, sample_rate,
                    &frames[count]);
        count++;
    }
    if (count == 0)
        return -1;

    found = sss_best_candidate(frames, count, pss->n_id_2, subframe5, score,
                               runner_up, scores);
    if (found < 0)
        return -1;
    *n_id_1 = found;
    return 0;
}

/*
 * Find the primary sequence's peak again, now that the whole tuning error is
 * known.
 *
 * The first search had to run without it, and a frequency offset does not
 * merely weaken a Zadoff-Chu correlation -- it moves it. The sequence is a
 * chirp, so an error in frequency and an error in time trade against each
 * other, and the peak lands somewhere other than the symbol boundary. On the
 * band 20 captures here the displacement is about seven samples, which is
 * inside the cyclic prefix and so does no harm to the secondary sequence, and
 * a great deal of harm to everything after it: seven samples is a phase ramp
 * of a third of a turn per subcarrier across the broadcast channel, which
 * the channel estimate cannot follow.
 *
 * With the offset removed the correlation can also be taken whole rather than
 * in two halves, which is what the halves were only ever a workaround for.
 */
static size_t pss_refine_timing(const float *i_samples, const float *q_samples,
                                size_t pair_count, double sample_rate,
                                double offset_hz, int n_id_2, size_t around,
                                float *peak, float *sidelobe) {
    struct pss_reference reference;
    double step = -2.0 * M_PI * offset_hz / sample_rate;
    long from = (long)around - LTE_TIMING_SEARCH;
    long to = (long)around + LTE_TIMING_SEARCH;
    size_t best_start = around;
    float best = -1.0f;
    long start;
    /* Every score the walk produces, so the sidelobe can be taken relative to
       wherever the peak finally lands -- which is not known until the walk is
       over, so it cannot be decided as we go. */
    float score[2 * LTE_TIMING_SEARCH + 1];
    int count = 0;

    pss_reference_build(n_id_2, &reference);
    if (from < 0)
        from = 0;
    while (to + LTE_FFT_SIZE > (long)pair_count)
        to--;

    for (start = from; start <= to; start++) {
        double cr = 0.0, ci = 0.0, energy = 0.0, magnitude;
        int n;
        for (n = 0; n < LTE_FFT_SIZE; n++) {
            double phase = step * (double)n;
            float c = (float)cos(phase), s = (float)sin(phase);
            float xr = i_samples[start + n], xi = q_samples[start + n];
            float dr = xr * c - xi * s;   /* the offset taken out */
            float di = xr * s + xi * c;
            cr += (double)dr * reference.re[n] + (double)di * reference.im[n];
            ci += (double)di * reference.re[n] - (double)dr * reference.im[n];
            energy += (double)dr * dr + (double)di * di;
        }
        if (energy <= 0.0)
            continue;
        magnitude = sqrt(cr * cr + ci * ci) /
                    sqrt(energy * (reference.half_energy[0] +
                                   reference.half_energy[1]));
        if (count < (int)(sizeof(score) / sizeof(score[0])))
            score[count++] = (float)magnitude;
        if ((float)magnitude > best) {
            best = (float)magnitude;
            best_start = (size_t)start;
        }
    }
    if (peak)
        *peak = best;
    if (sidelobe) {
        int peak_at = (int)((long)best_start - from);
        int n;
        float worst = 0.0f;
        for (n = 0; n < count; n++) {
            if (n - peak_at <= -LTE_TIMING_GUARD ||
                n - peak_at >= LTE_TIMING_GUARD) {
                if (score[n] > worst)
                    worst = score[n];
            }
        }
        *sidelobe = worst;
    }
    return best_start;
}

/* Defined with the broadcast channel below, because it reads the same
   reference signals. */
static double refine_offset(const float *i_samples, const float *q_samples,
                            double sample_rate, int pci,
                            size_t subframe0_start, double coarse);

int lte_cell_search(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    struct lte_cell *cell, struct lte_trace *trace) {
    struct lte_pss_result pss;
    float scores[2][LTE_N_ID_1_COUNT];
    float winning_scores[LTE_N_ID_1_COUNT];
    int leads[2] = { LTE_SSS_LEAD_NORMAL, LTE_SSS_LEAD_EXTENDED };
    int best_index = -1, best_id = 0, best_half = 0, best_integer = 0;
    float best_score = -2.0f, best_runner_up = 0.0f;
    /* What each cyclic-prefix hypothesis reached, kept apart from the winner
       so a verdict of "normal" can be read beside the number that beat. */
    float cp_best[2] = { 0.0f, 0.0f };
    int cp_seen[2] = { 0, 0 };
    double offset;
    long start;
    int k, integer;

    if (!cell)
        return -1;
    memset(cell, 0, sizeof(*cell));
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        memset(scores, 0, sizeof(scores));
    }
    if (lte_pss_detect(i_samples, q_samples, pair_count, sample_rate,
                       &pss, trace) <= 0)
        return 0;

    /*
     * Both cyclic prefixes, and every whole-subcarrier tuning error the
     * primary sequence could not report.
     *
     * The integer sweep is not an optimisation, it is the other half of the
     * frequency measurement. Two subcarriers of error leaves the primary
     * correlation standing -- degraded, but well over the floor -- while
     * putting every subcarrier the secondary sequence needs two places from
     * where it is read, so the search confidently returns a wrong identity or
     * none. On the captures here the answer is two, and finding it turns an
     * agreement of 36 of 61 into 52.
     */
    for (k = 0; k < 2 && best_score < LTE_SSS_CONFIDENT; k++) {
        int step;
        /* Outward from zero -- 0, -1, +1, -2, +2 -- so the common answer is
           reached in a few hypotheses rather than after all eleven. */
        for (step = 0; step <= 2 * LTE_INTEGER_OFFSETS; step++) {
            int n_id_1 = 0, subframe5 = 0;
            float score = 0.0f, runner_up = 0.0f;
            integer = (step + 1) / 2;
            if (step % 2)
                integer = -integer;
            if (best_score >= LTE_SSS_CONFIDENT &&
                best_score - best_runner_up >= LTE_SSS_MIN_MARGIN)
                break;
            offset = pss.frequency_offset_hz +
                     (double)integer * LTE_SUBCARRIER_SPACING_HZ;
            if (sss_try(i_samples, q_samples, pair_count, &pss, sample_rate,
                        offset, leads[k], &n_id_1, &subframe5, &score,
                        &runner_up, trace ? scores[k] : NULL) < 0)
                continue;
            if (!cp_seen[k] || score > cp_best[k]) {
                cp_best[k] = score;
                cp_seen[k] = 1;
            }
            if (score > best_score) {
                best_score = score;
                best_runner_up = runner_up;
                best_index = k;
                best_id = n_id_1;
                best_half = subframe5;
                best_integer = integer;
                if (trace)
                    memcpy(winning_scores, scores[k], sizeof(winning_scores));
            }
        }
    }
    /* Whatever happens next, say what was measured. A refusal that reports
       nothing cannot be told from a band with no cell in it. */
    cell->pss_correlation = pss.peak;
    cell->pss_runner_up = pss.runner_up;
    cell->sss_correlation = best_score < 0.0f ? 0.0f : best_score;
    cell->sss_runner_up = best_runner_up;
    cell->frequency_offset_hz = pss.frequency_offset_hz;
    if (best_index < 0 || best_score < LTE_SSS_MIN_CORRELATION ||
        best_score - best_runner_up < LTE_SSS_MIN_MARGIN)
        return 0;

    /*
     * The tuning error is known now, so the primary sequence's peak can be
     * found properly -- see pss_refine_timing. Everything below is measured
     * from it, so this has to happen before the frame boundary is worked out.
     */
    offset = pss.frequency_offset_hz +
             (double)best_integer * LTE_SUBCARRIER_SPACING_HZ;

    /*
     * The hypothesis the search never reached, measured now that it cannot
     * change anything.
     *
     * The loop above stops at the first prefix that clears LTE_SSS_CONFIDENT,
     * so on a strong cell the other one is not merely beaten, it is unasked --
     * and "normal CP" was printed as though a comparison had happened. Doing
     * it here rather than inside the loop is deliberate: the decision keeps
     * exactly the early exit it had, and this cannot promote a hypothesis the
     * search declined to try.
     *
     * Unconditional, and cheap for the same reason the early exit is not: what
     * that exit avoids is up to eleven integer hypotheses times two prefixes,
     * and this is one secondary-sequence read at an offset already known.
     */
    {
        int other = best_index == 0 ? 1 : 0;
        if (!cp_seen[other]) {
            int n_id_1 = 0, subframe5 = 0;
            float score = 0.0f, runner_up = 0.0f;
            if (sss_try(i_samples, q_samples, pair_count, &pss, sample_rate,
                        offset, leads[other], &n_id_1, &subframe5, &score,
                        &runner_up, NULL) == 0) {
                cp_best[other] = score;
                cp_seen[other] = 1;
            }
        }
    }

    {
        float refined_peak = 0.0f, sidelobe = 0.0f;
        size_t refined = pss_refine_timing(i_samples, q_samples, pair_count,
                                           sample_rate, offset, pss.n_id_2,
                                           pss.useful_start, &refined_peak,
                                           &sidelobe);
        cell->timing_sidelobe = sidelobe;
        if (refined_peak > pss.peak) {
            cell->timing_shift = (int)((long)refined -
                                       (long)pss.useful_start);
            pss.useful_start = refined;
            pss.peak = refined_peak;
        }
    }

    /*
     * Where the frame begins. The PSS found sits at a known offset into its
     * subframe, and SSS has just said which subframe that was; a PSS from
     * subframe 5 puts subframe 0 five subframes earlier, which may be before
     * these samples started, in which case the next frame's is the one to
     * report.
     */
    start = (long)pss.useful_start - LTE_PSS_USEFUL_OFFSET -
            (best_half ? LTE_HALF_FRAME_SAMPLES : 0);
    while (start < 0)
        start += LTE_FRAME_SAMPLES;
    if ((size_t)start + LTE_SUBFRAME_SAMPLES > pair_count)
        return 0;

    cell->detected = 1;
    cell->n_id_1 = best_id;
    cell->n_id_2 = pss.n_id_2;
    cell->pci = 3 * best_id + pss.n_id_2;
    cell->extended_cp = (best_index == 1);
    cell->half_frame = best_half;
    cell->subframe0_start = (size_t)start;
    cell->integer_offset = best_integer;
    /* The whole-subcarrier part the primary sequence could not see, plus the
       fraction it could. Only together are they the tuning error. */
    cell->frequency_offset_hz = pss.frequency_offset_hz +
                                (double)best_integer *
                                    LTE_SUBCARRIER_SPACING_HZ;
    /* The reference signals are only where the normal prefix puts them, and
       an extended-prefix cell keeps the coarser reading. */
    if (!cell->extended_cp)
        cell->frequency_offset_hz =
            refine_offset(i_samples, q_samples, sample_rate, cell->pci,
                          cell->subframe0_start, cell->frequency_offset_hz);
    cell->pss_correlation = pss.peak;
    cell->pss_runner_up = pss.runner_up;
    cell->sss_correlation = best_score;
    cell->sss_runner_up = best_runner_up;
    cell->cp_score[0] = cp_best[0];
    cell->cp_score[1] = cp_best[1];
    cell->cp_measured[0] = cp_seen[0];
    cell->cp_measured[1] = cp_seen[1];
    if (trace) {
        memcpy(trace->candidate, winning_scores, sizeof(trace->candidate));
        trace->candidate_count = LTE_N_ID_1_COUNT;
        trace->candidate_best = best_id;
        trace->valid = 1;
    }
    return 1;
}


/* ------------------------------------------------------------------ */
/* Cell-specific reference signals.                                   */
/* ------------------------------------------------------------------ */

/*
 * Which symbol of a slot carries reference signals for a port, and with what
 * frequency shift (36.211 table 6.10.1.2-1). Returns 0 when that port has
 * nothing in that symbol.
 *
 * Ports 0 and 1 appear twice in a slot, at symbols 0 and 4, swapping shifts
 * between the two; ports 2 and 3 appear once, at symbol 1. The symbol numbers
 * are the normal cyclic prefix's -- the extended one has six symbols to a slot
 * and puts the second pair at symbol 3 -- and nothing in this file reads an
 * extended-prefix cell.
 */
static int crs_shift(int slot, int symbol, int port, int *v) {
    int slot_parity = slot % 2;
    if (port == 0 || port == 1) {
        int first = (port == 0) ? 0 : 3;
        if (symbol == 0) { *v = first; return 1; }
        if (symbol == 4) { *v = 3 - first; return 1; }
        return 0;
    }
    if (port == 2 && symbol == 1) { *v = 3 * slot_parity; return 1; }
    if (port == 3 && symbol == 1) { *v = 3 + 3 * slot_parity; return 1; }
    return 0;
}

int lte_crs_subcarriers(int pci, int slot, int symbol, int port,
                        int *indices) {
    int v, shift, first, m;
    if (!indices || pci < 0 || pci >= LTE_PCI_COUNT || port < 0 || port > 3)
        return 0;
    if (!crs_shift(slot, symbol, port, &v))
        return 0;
    /*
     * A reference signal sits every sixth subcarrier at an offset the cell
     * identity picks. The central 72 subcarriers start at a multiple of six
     * whatever the carrier's bandwidth -- 6 * (N_RB - 6) -- so the offset
     * within them is the same as the offset in the whole grid, and this holds
     * without knowing a bandwidth the MIB has not yet been read to learn.
     */
    shift = pci % 6;
    first = (v + shift) % 6;
    for (m = 0; m < LTE_PBCH_SUBCARRIERS / 6; m++)
        indices[m] = first + 6 * m;
    return LTE_PBCH_SUBCARRIERS / 6;
}

int lte_crs_sequence(int pci, int slot, int symbol, int port, int extended_cp,
                     float *real, float *imag) {
    /* The central twelve reference signals are always these twelve indices
       into the sequence: the sequence is laid out from the widest bandwidth
       the standard allows inwards, so the middle of the carrier lands in the
       same place whatever its real width. */
    const int central_first = 104;
    uint8_t c[2 * 116];
    uint32_t c_init;
    int v, m;

    if (!real || !imag || pci < 0 || pci >= LTE_PCI_COUNT ||
        port < 0 || port > 3)
        return 0;
    if (!crs_shift(slot, symbol, port, &v))
        return 0;

    c_init = (uint32_t)(1024 * (7 * (slot + 1) + symbol + 1) *
                        (2 * pci + 1) + 2 * pci + (extended_cp ? 0 : 1));
    lte_gold_sequence(c_init, 2 * 116, c);

    for (m = 0; m < LTE_PBCH_SUBCARRIERS / 6; m++) {
        int index = central_first + m;
        real[m] = (float)((1 - 2 * (int)c[2 * index]) / sqrt(2.0));
        imag[m] = (float)((1 - 2 * (int)c[2 * index + 1]) / sqrt(2.0));
    }
    return LTE_PBCH_SUBCARRIERS / 6;
}


/* ------------------------------------------------------------------ */
/* The broadcast channel's resource elements.                          */
/* ------------------------------------------------------------------ */

/* The physical subcarrier a broadcast-channel index sits on: 36 either side
   of DC, which is skipped. */
static int pbch_subcarrier(int index) {
    return (index < 36) ? index - 36 : index - 35;
}

/* Useful-part offset of one symbol of slot 1, from the start of subframe 0,
   under the normal cyclic prefix. */
static size_t slot1_useful_offset(int symbol) {
    if (symbol == 0)
        return LTE_SLOT_SAMPLES + LTE_CP_FIRST_SAMPLES;
    return LTE_SLOT_SAMPLES + LTE_CP_FIRST_SAMPLES + LTE_FFT_SIZE +
           (size_t)(symbol - 1) * (LTE_CP_REST_SAMPLES + LTE_FFT_SIZE) +
           LTE_CP_REST_SAMPLES;
}

/* Frequency-interpolate a channel estimate taken every sixth subcarrier out
   to all 72, holding the end values past the outermost reference. */
static void interpolate_channel(const int *positions, int count,
                                const float *taken_re, const float *taken_im,
                                float *out_re, float *out_im) {
    int index, k;
    for (index = 0; index < LTE_PBCH_SUBCARRIERS; index++) {
        if (index <= positions[0]) {
            out_re[index] = taken_re[0];
            out_im[index] = taken_im[0];
            continue;
        }
        if (index >= positions[count - 1]) {
            out_re[index] = taken_re[count - 1];
            out_im[index] = taken_im[count - 1];
            continue;
        }
        for (k = 0; k < count - 1; k++) {
            if (index >= positions[k] && index <= positions[k + 1]) {
                float span = (float)(positions[k + 1] - positions[k]);
                float t = (float)(index - positions[k]) / span;
                out_re[index] = taken_re[k] * (1.0f - t) + taken_re[k + 1] * t;
                out_im[index] = taken_im[k] * (1.0f - t) + taken_im[k + 1] * t;
                break;
            }
        }
    }
}

/* The central 72 subcarriers of one symbol of slot 1, with the frequency
   offset taken out. */
static void read_slot1_symbol(const float *i_samples, const float *q_samples,
                              size_t subframe0_start, int symbol,
                              double offset_hz, double sample_rate,
                              float *row_re, float *row_im) {
    float full_re[LTE_FFT_SIZE], full_im[LTE_FFT_SIZE];
    int index;
    symbol_fft_corrected(i_samples, q_samples,
                         subframe0_start + slot1_useful_offset(symbol), 0,
                         offset_hz, sample_rate, full_re, full_im);
    for (index = 0; index < LTE_PBCH_SUBCARRIERS; index++) {
        int bin = lte_subcarrier_bin(pbch_subcarrier(index));
        row_re[index] = full_re[bin];
        row_im[index] = full_im[bin];
    }
}

/* One port's channel across the broadcast channel's 72 subcarriers, measured
   from the reference signals of one symbol and interpolated between them.
   Returns 0 when the port has no reference signal in that symbol. */
static int estimate_port(const float *row_re, const float *row_im,
                         int pci, int symbol, int port,
                         float *out_re, float *out_im) {
    int positions[LTE_PBCH_SUBCARRIERS / 6];
    float ref_re[LTE_PBCH_SUBCARRIERS / 6], ref_im[LTE_PBCH_SUBCARRIERS / 6];
    float taken_re[LTE_PBCH_SUBCARRIERS / 6], taken_im[LTE_PBCH_SUBCARRIERS / 6];
    int count, m;

    count = lte_crs_subcarriers(pci, 1, symbol, port, positions);
    if (count <= 0)
        return 0;
    if (lte_crs_sequence(pci, 1, symbol, port, 0, ref_re, ref_im) <= 0)
        return 0;

    for (m = 0; m < count; m++) {
        int k = positions[m];
        /* The reference symbols have unit magnitude, so dividing by one is
           multiplying by its conjugate. */
        taken_re[m] = row_re[k] * ref_re[m] + row_im[k] * ref_im[m];
        taken_im[m] = row_im[k] * ref_re[m] - row_re[k] * ref_im[m];
    }
    interpolate_channel(positions, count, taken_re, taken_im, out_re, out_im);
    return 1;
}

/*
 * A second, far finer reading of the frequency error, from the reference
 * signals rather than from PSS.
 *
 * PSS measures the offset from the phase turned across 64 samples, which is
 * all a detector has before it knows anything at all; 64 samples is a short
 * lever and the answer scatters by several hundred hertz at the levels a real
 * antenna gives. Once the cell identity is known, the reference signals of
 * slot 1 can be read at symbol 0 and again at symbol 4 -- 548 samples apart,
 * eight times the lever, and twenty-four known symbols to average rather than
 * one correlation peak. It stays unambiguous to +-1.75 kHz, which covers
 * where PSS leaves off with room to spare.
 *
 * The two symbols do not put a port's references on the same subcarriers --
 * ports 0 and 1 swap shifts between them -- so each is interpolated across
 * the whole width first and the two are compared there. Comparing them where
 * they were measured would read the channel's own slope as a frequency error.
 */
static double refine_offset(const float *i_samples, const float *q_samples,
                            double sample_rate, int pci,
                            size_t subframe0_start, double coarse) {
    const int lag = 4 * (LTE_CP_REST_SAMPLES + LTE_FFT_SIZE);
    float early_row_re[LTE_PBCH_SUBCARRIERS], early_row_im[LTE_PBCH_SUBCARRIERS];
    float late_row_re[LTE_PBCH_SUBCARRIERS], late_row_im[LTE_PBCH_SUBCARRIERS];
    double sum_re = 0.0, sum_im = 0.0;
    int port;

    read_slot1_symbol(i_samples, q_samples, subframe0_start, 0, coarse,
                      sample_rate, early_row_re, early_row_im);
    read_slot1_symbol(i_samples, q_samples, subframe0_start, 4, coarse,
                      sample_rate, late_row_re, late_row_im);

    for (port = 0; port < 2; port++) {
        float early_re[LTE_PBCH_SUBCARRIERS], early_im[LTE_PBCH_SUBCARRIERS];
        float late_re[LTE_PBCH_SUBCARRIERS], late_im[LTE_PBCH_SUBCARRIERS];
        int k;
        if (!estimate_port(early_row_re, early_row_im, pci, 0, port,
                           early_re, early_im))
            continue;
        if (!estimate_port(late_row_re, late_row_im, pci, 4, port,
                           late_re, late_im))
            continue;
        for (k = 0; k < LTE_PBCH_SUBCARRIERS; k++) {
            sum_re += (double)late_re[k] * early_re[k] +
                      (double)late_im[k] * early_im[k];
            sum_im += (double)late_im[k] * early_re[k] -
                      (double)late_re[k] * early_im[k];
        }
    }

    if (sum_re == 0.0 && sum_im == 0.0)
        return coarse;
    return coarse + atan2(sum_im, sum_re) * sample_rate /
                    (2.0 * M_PI * (double)lag);
}

/* Two received elements and the two channels they crossed, undone. The
   Alamouti pair of a space-frequency block code: each output is the sum of
   two matched filters, and the interference between them cancels. */
static void alamouti(float r0re, float r0im, float r1re, float r1im,
                     float h0re, float h0im, float h1re, float h1im,
                     float *x0re, float *x0im, float *x1re, float *x1im) {
    /* x0 = conj(h0) * r0 + h1 * conj(r1) */
    *x0re = h0re * r0re + h0im * r0im + h1re * r1re + h1im * r1im;
    *x0im = h0re * r0im - h0im * r0re + h1im * r1re - h1re * r1im;
    /* x1 = conj(h1) * r0 - h0 * conj(r1) */
    *x1re = h1re * r0re + h1im * r0im - h0re * r1re - h0im * r1im;
    *x1im = h1re * r0im - h1im * r0re - h0im * r1re + h0re * r1im;
}

int lte_pbch_soft_bits(const float *i_samples, const float *q_samples,
                       size_t pair_count, double sample_rate,
                       const struct lte_cell *cell, size_t subframe0_start,
                       int antenna_ports, float *soft_bits,
                       struct lte_trace *trace) {
    float grid_re[LTE_PBCH_SYMBOLS][LTE_PBCH_SUBCARRIERS];
    float grid_im[LTE_PBCH_SYMBOLS][LTE_PBCH_SUBCARRIERS];
    float channel_re[4][LTE_PBCH_SUBCARRIERS];
    float channel_im[4][LTE_PBCH_SUBCARRIERS];
    /* The resource elements the broadcast channel actually uses, and where in
       the grid each came from, in the order the standard maps them. */
    float re_re[LTE_PBCH_RESOURCE_ELEMENTS], re_im[LTE_PBCH_RESOURCE_ELEMENTS];
    int re_index[LTE_PBCH_RESOURCE_ELEMENTS];
    int shift, count = 0;
    int symbol, index, port, out = 0;

    if (!cell || !soft_bits || !i_samples || !q_samples)
        return 0;
    if (fabs(sample_rate - LTE_SAMPLE_RATE_HZ) > 1.0)
        return 0;
    if (antenna_ports != 1 && antenna_ports != 2 && antenna_ports != 4)
        return 0;
    /*
     * The extended cyclic prefix puts a third reference symbol inside the
     * broadcast channel and shortens it to 432 bits. No commercial FDD cell
     * uses it, and pretending to decode one would mean writing a second
     * mapping nothing could be checked against; the cell search reports the
     * prefix it found and this declines the ones it cannot read.
     */
    if (cell->extended_cp)
        return 0;
    if (subframe0_start + LTE_SUBFRAME_SAMPLES > pair_count)
        return 0;

    for (symbol = 0; symbol < LTE_PBCH_SYMBOLS; symbol++)
        read_slot1_symbol(i_samples, q_samples, subframe0_start, symbol,
                          cell->frequency_offset_hz, sample_rate,
                          grid_re[symbol], grid_im[symbol]);

    /* Ports 0 and 1 are measured in the first symbol, ports 2 and 3 in the
       second -- the only symbols of the four that carry references at all. */
    for (port = 0; port < 4; port++)
        if (!estimate_port(grid_re[port < 2 ? 0 : 1], grid_im[port < 2 ? 0 : 1],
                           cell->pci, port < 2 ? 0 : 1, port,
                           channel_re[port], channel_im[port]))
            return 0;

    if (trace) {
        /* The channel one port measured, relative to its own mean, which is
           what makes two captures comparable. A deep notch here is why a
           block does not decode, and nothing else on the screen shows it. */
        double mean = 0.0;
        int k;
        for (k = 0; k < LTE_PBCH_SUBCARRIERS; k++)
            mean += sqrt((double)channel_re[0][k] * channel_re[0][k] +
                         (double)channel_im[0][k] * channel_im[0][k]);
        mean /= LTE_PBCH_SUBCARRIERS;
        for (k = 0; k < LTE_PBCH_SUBCARRIERS; k++) {
            double m = sqrt((double)channel_re[0][k] * channel_re[0][k] +
                            (double)channel_im[0][k] * channel_im[0][k]);
            trace->channel_db[k] = (mean > 0.0 && m > 0.0)
                                       ? (float)(20.0 * log10(m / mean))
                                       : -40.0f;
        }
        trace->channel_count = LTE_PBCH_SUBCARRIERS;
    }

    /*
     * Collect the elements the broadcast channel is mapped to: increasing
     * subcarrier first, then symbol, skipping every subcarrier a reference
     * signal could occupy. "Could" is the point -- the mapping leaves room for
     * four ports whatever the cell transmits on, so a one-port cell has the
     * same holes as a four-port one and a receiver need not know which it is
     * before it can find the bits.
     */
    shift = cell->pci % 6;
    for (symbol = 0; symbol < LTE_PBCH_SYMBOLS; symbol++) {
        for (index = 0; index < LTE_PBCH_SUBCARRIERS; index++) {
            if (symbol < 2 && index % 3 == shift % 3)
                continue;
            if (count >= LTE_PBCH_RESOURCE_ELEMENTS)
                return 0;
            re_re[count] = grid_re[symbol][index];
            re_im[count] = grid_im[symbol][index];
            re_index[count] = index;
            count++;
        }
    }
    if (count != LTE_PBCH_RESOURCE_ELEMENTS)
        return 0;

    if (antenna_ports == 1) {
        int n;
        for (n = 0; n < count; n++) {
            int k = re_index[n];
            float hr = channel_re[0][k], hi = channel_im[0][k];
            /* Matched filter: conj(h) * r. Its magnitude carries how much the
               channel was worth there, which is exactly the confidence a soft
               bit should have. */
            soft_bits[out++] = hr * re_re[n] + hi * re_im[n];
            soft_bits[out++] = hr * re_im[n] - hi * re_re[n];
        }
    } else if (antenna_ports == 2) {
        int n;
        for (n = 0; n + 1 < count; n += 2) {
            int k0 = re_index[n], k1 = re_index[n + 1];
            float h0re = 0.5f * (channel_re[0][k0] + channel_re[0][k1]);
            float h0im = 0.5f * (channel_im[0][k0] + channel_im[0][k1]);
            float h1re = 0.5f * (channel_re[1][k0] + channel_re[1][k1]);
            float h1im = 0.5f * (channel_im[1][k0] + channel_im[1][k1]);
            float x0re, x0im, x1re, x1im;
            alamouti(re_re[n], re_im[n], re_re[n + 1], re_im[n + 1],
                     h0re, h0im, h1re, h1im, &x0re, &x0im, &x1re, &x1im);
            soft_bits[out++] = x0re;
            soft_bits[out++] = x0im;
            soft_bits[out++] = x1re;
            soft_bits[out++] = x1im;
        }
    } else {
        int n;
        for (n = 0; n + 3 < count; n += 4) {
            /* Four ports alternate: the first pair of elements is carried by
               ports 0 and 2, the second by ports 1 and 3. */
            int pair, base[2] = { 0, 1 }, other[2] = { 2, 3 };
            for (pair = 0; pair < 2; pair++) {
                int a = n + 2 * pair, b = a + 1;
                int ka = re_index[a], kb = re_index[b];
                int p0 = base[pair], p1 = other[pair];
                float h0re = 0.5f * (channel_re[p0][ka] + channel_re[p0][kb]);
                float h0im = 0.5f * (channel_im[p0][ka] + channel_im[p0][kb]);
                float h1re = 0.5f * (channel_re[p1][ka] + channel_re[p1][kb]);
                float h1im = 0.5f * (channel_im[p1][ka] + channel_im[p1][kb]);
                float x0re, x0im, x1re, x1im;
                alamouti(re_re[a], re_im[a], re_re[b], re_im[b],
                         h0re, h0im, h1re, h1im, &x0re, &x0im, &x1re, &x1im);
                soft_bits[out++] = x0re;
                soft_bits[out++] = x0im;
                soft_bits[out++] = x1re;
                soft_bits[out++] = x1im;
            }
        }
    }

    if (out != LTE_PBCH_SOFT_BITS)
        return 0;

    if (trace) {
        /* Scaled so the typical point sits at the unit circle, which is what
           the constellation component expects and what makes a cloud
           distinguishable from four corners by eye. */
        double mean = 0.0;
        int k;
        for (k = 0; k < LTE_PBCH_RESOURCE_ELEMENTS; k++)
            mean += sqrt((double)soft_bits[2 * k] * soft_bits[2 * k] +
                         (double)soft_bits[2 * k + 1] * soft_bits[2 * k + 1]);
        mean /= LTE_PBCH_RESOURCE_ELEMENTS;
        if (mean <= 0.0)
            mean = 1.0;
        for (k = 0; k < LTE_PBCH_RESOURCE_ELEMENTS; k++) {
            trace->element_i[k] = (float)(soft_bits[2 * k] / mean * 0.7071);
            trace->element_q[k] = (float)(soft_bits[2 * k + 1] / mean * 0.7071);
            trace->element_bit[k] = (unsigned char)(soft_bits[2 * k] > 0.0f
                                                        ? 0 : 1);
        }
        trace->element_count = LTE_PBCH_RESOURCE_ELEMENTS;
    }
    return out;
}
