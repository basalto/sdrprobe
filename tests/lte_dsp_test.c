#include "check.h"

#include "lte_dsp.h"
#include "lte_gold.h"
#include "lte_mib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The LTE plugin: the channel map, the three sequences the standard fixes, and
 * a cell search run against a frame this file builds.
 *
 * The synthesised frame is the point of the suite. Every mapping the plugin
 * relies on -- which subcarrier a sequence element sits on, which symbol
 * carries the reference signals, where in the subframe the broadcast channel
 * is -- is written out here a second time and independently, from the
 * standard rather than from lte_dsp.c. A transform and its inverse sharing a
 * mistake would still round-trip; two spellings of the same mapping agreeing
 * is worth something. The one place that is deliberately not independent is
 * lte_subcarrier_bin, which is checked against its six values by hand below
 * and then used, because writing it twice would prove nothing.
 */

#define FRAME_SYMBOLS 140
#define FRAMES 2
#define LEAD_IN 3000
#define BUFFER_SAMPLES (LEAD_IN + FRAMES * LTE_FRAME_SAMPLES + 2000)

static float grid_re[FRAME_SYMBOLS][LTE_FFT_SIZE];
static float grid_im[FRAME_SYMBOLS][LTE_FFT_SIZE];
static float buffer_i[BUFFER_SAMPLES];
static float buffer_q[BUFFER_SAMPLES];
static uint8_t pbch_bits[LTE_PBCH_SOFT_BITS];

/* Deterministic noise, so a failure is the same failure next time. */
static uint32_t rng_state;

static void rng_seed(uint32_t seed) { rng_state = seed ? seed : 1u; }

static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static double rng_uniform(void) {
    return ((double)(rng_next() >> 8) + 0.5) / 16777216.0;
}

static double rng_normal(void) {
    double u = rng_uniform(), v = rng_uniform();
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}


/* ------------------------------------------------------------------ */
/* The frame's geometry, written from the standard rather than read    */
/* from lte_dsp.c.                                                     */
/* ------------------------------------------------------------------ */

/* Useful-part start of a symbol, counted from the frame. Normal cyclic
   prefix: ten samples before the first symbol of a slot and nine before each
   of the other six, which fills the slot's 960 exactly. */
static size_t useful_start(int symbol_index) {
    int slot = symbol_index / 7;
    int l = symbol_index % 7;
    size_t base = (size_t)slot * LTE_SLOT_SAMPLES;
    if (l == 0)
        return base + 10;
    return base + 10 + LTE_FFT_SIZE + (size_t)(l - 1) * (9 + LTE_FFT_SIZE) + 9;
}

static int cp_length(int symbol_index) {
    return (symbol_index % 7) == 0 ? 10 : 9;
}

/* Synchronisation element n -> physical subcarrier: 31 either side of the
   unused DC. */
static int sync_subcarrier_of(int n) { return (n < 31) ? n - 31 : n - 30; }

/* Broadcast-channel element -> physical subcarrier: 36 either side. */
static int pbch_subcarrier_of(int index) {
    return (index < 36) ? index - 36 : index - 35;
}


/* ------------------------------------------------------------------ */
/* Channels, one per antenna port.                                     */
/* ------------------------------------------------------------------ */

static const double port_gain[4] = { 0.80, 0.55, 0.42, 0.37 };
static const double port_phase[4] = { 0.4, -1.9, 2.6, -0.7 };
/* A gentle tilt across the band, so the estimate has to interpolate between
   reference signals rather than copy a constant. */
static const double port_tilt[4] = { 0.5, -0.4, 0.3, -0.6 };

static void channel_at(int port, int subcarrier, double *re, double *im) {
    double phase = port_phase[port] + port_tilt[port] * (double)subcarrier / 36.0;
    *re = port_gain[port] * cos(phase);
    *im = port_gain[port] * sin(phase);
}

static void grid_add(int symbol, int subcarrier, int port,
                     double re, double im) {
    int bin = lte_subcarrier_bin(subcarrier);
    double hr, hi;
    channel_at(port, subcarrier, &hr, &hi);
    grid_re[symbol][bin] += (float)(re * hr - im * hi);
    grid_im[symbol][bin] += (float)(re * hi + im * hr);
}

static void grid_set_silent(int symbol, int subcarrier) {
    int bin = lte_subcarrier_bin(subcarrier);
    grid_re[symbol][bin] = 0.0f;
    grid_im[symbol][bin] = 0.0f;
}


/* ------------------------------------------------------------------ */
/* Building a frame.                                                   */
/* ------------------------------------------------------------------ */

/* Precomputed roots of unity: only 128 distinct angles appear. */
static double twiddle_re[LTE_FFT_SIZE];
static double twiddle_im[LTE_FFT_SIZE];

static void twiddles_init(void) {
    int n;
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        twiddle_re[n] = cos(2.0 * M_PI * (double)n / (double)LTE_FFT_SIZE);
        twiddle_im[n] = sin(2.0 * M_PI * (double)n / (double)LTE_FFT_SIZE);
    }
}

/* A plain inverse transform, quadratic and obvious. It is the OFDM modulator
   here and, in test_fft_matches_dft, the yardstick the plugin's radix-2 FFT is
   held against. */
static void idft(const float *re, const float *im, float *out_re,
                 float *out_im) {
    int n, k;
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        double sr = 0.0, si = 0.0;
        for (k = 0; k < LTE_FFT_SIZE; k++) {
            int angle = (k * n) % LTE_FFT_SIZE;
            sr += (double)re[k] * twiddle_re[angle] -
                  (double)im[k] * twiddle_im[angle];
            si += (double)re[k] * twiddle_im[angle] +
                  (double)im[k] * twiddle_re[angle];
        }
        out_re[n] = (float)(sr / LTE_FFT_SIZE);
        out_im[n] = (float)(si / LTE_FFT_SIZE);
    }
}

static void qpsk(int b0, int b1, double *re, double *im) {
    *re = (1 - 2 * b0) / sqrt(2.0);
    *im = (1 - 2 * b1) / sqrt(2.0);
}

/* Whether a subcarrier of a broadcast-channel symbol is reserved for a
   reference signal. All four ports' positions are reserved whatever the cell
   transmits on, which is what lets a receiver find the bits before it knows
   the port count. */
static int is_reference_position(int pci, int symbol, int index) {
    return symbol < 2 && (index % 3) == (pci % 6) % 3;
}

static void fill_other_traffic(void) {
    int symbol, sc;
    for (symbol = 0; symbol < FRAME_SYMBOLS; symbol++) {
        for (sc = -50; sc <= 50; sc++) {
            double re, im;
            if (sc == 0)
                continue;
            qpsk((int)(rng_next() & 1u), (int)(rng_next() & 1u), &re, &im);
            grid_add(symbol, sc, 0, re * 0.9, im * 0.9);
        }
    }
}

static void place_sync(int pci) {
    int n_id_2 = pci % 3;
    int n_id_1 = pci / 3;
    float pss_re[LTE_SYNC_SUBCARRIERS], pss_im[LTE_SYNC_SUBCARRIERS];
    float sss[LTE_SYNC_SUBCARRIERS];
    int half, n;

    lte_pss_sequence(n_id_2, pss_re, pss_im);
    for (half = 0; half < 2; half++) {
        /* Last symbol of slot 0 and of slot 10; the one before each carries
           the secondary sequence. */
        int pss_symbol = half ? 76 : 6;
        int sss_symbol = pss_symbol - 1;
        lte_sss_sequence(n_id_1, n_id_2, half, sss);
        for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++) {
            int sc = sync_subcarrier_of(n);
            grid_set_silent(pss_symbol, sc);
            grid_set_silent(sss_symbol, sc);
            grid_add(pss_symbol, sc, 0, pss_re[n], pss_im[n]);
            grid_add(sss_symbol, sc, 0, sss[n], 0.0);
        }
        /*
         * And the five either side, which 36.211 clause 6.11.1.2 reserves and
         * does not transmit. `fill_other_traffic` writes across the whole
         * span, so without this the buffer carries data where the air carries
         * nothing -- and those ten resource elements are the only place a
         * receiver can measure noise without a channel in the way.
         */
        for (n = LTE_SYNC_SUBCARRIERS / 2; n < LTE_SYNC_SUBCARRIERS / 2 + 5;
             n++) {
            grid_set_silent(pss_symbol, n + 1);
            grid_set_silent(sss_symbol, n + 1);
            grid_set_silent(pss_symbol, -(n + 1));
            grid_set_silent(sss_symbol, -(n + 1));
        }
    }
}

/* One port's reference signals in one symbol of slot 1, written from
   36.211 table 6.10.1.2-1 rather than from the plugin's copy of it. */
static void place_reference_signals(int pci, int port, int symbol_in_slot) {
    float ref_re[LTE_PBCH_SUBCARRIERS / 6], ref_im[LTE_PBCH_SUBCARRIERS / 6];
    int shift = pci % 6;
    int v, first, m;

    if (port == 0)
        v = (symbol_in_slot == 0) ? 0 : 3;
    else if (port == 1)
        v = (symbol_in_slot == 0) ? 3 : 0;
    else if (port == 2)
        v = 3;                /* 3 * (slot 1 is odd) */
    else
        v = 0;                /* 3 + 3, which comes round to nothing */
    first = (v + shift) % 6;

    lte_crs_sequence(pci, 1, symbol_in_slot, port, 0, ref_re, ref_im);
    for (m = 0; m < LTE_PBCH_SUBCARRIERS / 6; m++) {
        int subcarrier = pbch_subcarrier_of(first + 6 * m);
        grid_set_silent(7 + symbol_in_slot, subcarrier);
        grid_add(7 + symbol_in_slot, subcarrier, port, ref_re[m], ref_im[m]);
    }
}

static void place_broadcast(int pci, int ports) {
    double x_re[LTE_PBCH_RESOURCE_ELEMENTS], x_im[LTE_PBCH_RESOURCE_ELEMENTS];
    int symbol_of[LTE_PBCH_RESOURCE_ELEMENTS];
    int index_of[LTE_PBCH_RESOURCE_ELEMENTS];
    int symbol, index, n, count = 0, port;
    double root_half = 1.0 / sqrt(2.0);

    /* Clear the whole broadcast region, then write only what is transmitted:
       the reference positions of ports this cell actually has, and the data. */
    for (symbol = 0; symbol < LTE_PBCH_SYMBOLS; symbol++)
        for (index = 0; index < LTE_PBCH_SUBCARRIERS; index++)
            grid_set_silent(7 + symbol, pbch_subcarrier_of(index));

    /* Ports 0 and 1 twice in the slot, at symbols 0 and 4 with their shifts
       swapped; ports 2 and 3 once, at symbol 1. Symbol 4 is outside the
       broadcast channel but inside the subframe, and it is what the frequency
       refinement reads. */
    for (port = 0; port < ports && port < 2; port++) {
        place_reference_signals(pci, port, 0);
        place_reference_signals(pci, port, 4);
    }
    for (port = 2; port < ports; port++)
        place_reference_signals(pci, port, 1);

    for (symbol = 0; symbol < LTE_PBCH_SYMBOLS; symbol++) {
        for (index = 0; index < LTE_PBCH_SUBCARRIERS; index++) {
            if (is_reference_position(pci, symbol, index))
                continue;
            qpsk(pbch_bits[2 * count], pbch_bits[2 * count + 1],
                 &x_re[count], &x_im[count]);
            symbol_of[count] = 7 + symbol;
            index_of[count] = index;
            count++;
        }
    }

    if (ports == 1) {
        for (n = 0; n < count; n++)
            grid_add(symbol_of[n], pbch_subcarrier_of(index_of[n]), 0,
                     x_re[n], x_im[n]);
    } else if (ports == 2) {
        for (n = 0; n + 1 < count; n += 2) {
            int a = n, b = n + 1;
            int sca = pbch_subcarrier_of(index_of[a]);
            int scb = pbch_subcarrier_of(index_of[b]);
            /*
             * 36.211 table 6.3.4.3-1, and the placement is the whole point:
             * the *first* element carries x0 on port 0 and -conj(x1) on port
             * 1, the second carries x1 on port 0 and conj(x0) on port 1.
             *
             * This encoder had x1 and -conj(x1) transposed between the two
             * ports, which is exactly the error the decoder made, so the
             * round trip agreed perfectly and read nothing off the air.
             * `check_real_capture` is what pins this now.
             */
            grid_add(symbol_of[a], sca, 0, x_re[n] * root_half,
                     x_im[n] * root_half);
            grid_add(symbol_of[a], sca, 1, -x_re[n + 1] * root_half,
                     x_im[n + 1] * root_half);
            grid_add(symbol_of[b], scb, 0, x_re[n + 1] * root_half,
                     x_im[n + 1] * root_half);
            grid_add(symbol_of[b], scb, 1, x_re[n] * root_half,
                     -x_im[n] * root_half);
        }
    } else {
        for (n = 0; n + 3 < count; n += 4) {
            int pair;
            for (pair = 0; pair < 2; pair++) {
                int a = n + 2 * pair, b = a + 1;
                int p0 = pair, p1 = pair + 2;
                int sca = pbch_subcarrier_of(index_of[a]);
                int scb = pbch_subcarrier_of(index_of[b]);
                double f0re = x_re[a], f0im = x_im[a];
                double f1re = x_re[b], f1im = x_im[b];
                /* The same pair, on ports (0,2) then (1,3). */
                grid_add(symbol_of[a], sca, p0, f0re * root_half,
                         f0im * root_half);
                grid_add(symbol_of[a], sca, p1, -f1re * root_half,
                         f1im * root_half);
                grid_add(symbol_of[b], scb, p0, f1re * root_half,
                         f1im * root_half);
                grid_add(symbol_of[b], scb, p1, f0re * root_half,
                         -f0im * root_half);
            }
        }
    }
}

/*
 * Lay the frame twice into the buffer, so a search that lands in the second
 * half-frame still has a whole subframe 0 ahead of it, then rotate the whole
 * thing by the frequency offset and add noise.
 */
static void build_buffer(int pci, int ports, double offset_hz, double sigma,
                         uint32_t seed) {
    int symbol, frame, n;
    size_t k;

    rng_seed(seed);
    memset(grid_re, 0, sizeof(grid_re));
    memset(grid_im, 0, sizeof(grid_im));
    for (n = 0; n < LTE_PBCH_SOFT_BITS; n++)
        pbch_bits[n] = (uint8_t)(rng_next() & 1u);

    fill_other_traffic();
    place_sync(pci);
    place_broadcast(pci, ports);

    memset(buffer_i, 0, sizeof(buffer_i));
    memset(buffer_q, 0, sizeof(buffer_q));
    for (frame = 0; frame < FRAMES; frame++) {
        size_t frame_base = (size_t)LEAD_IN + (size_t)frame * LTE_FRAME_SAMPLES;
        for (symbol = 0; symbol < FRAME_SYMBOLS; symbol++) {
            float time_re[LTE_FFT_SIZE], time_im[LTE_FFT_SIZE];
            size_t start = frame_base + useful_start(symbol);
            int cp = cp_length(symbol);
            idft(grid_re[symbol], grid_im[symbol], time_re, time_im);
            for (n = 0; n < LTE_FFT_SIZE; n++) {
                buffer_i[start + (size_t)n] = time_re[n];
                buffer_q[start + (size_t)n] = time_im[n];
            }
            /* The cyclic prefix is the tail of the symbol repeated. */
            for (n = 0; n < cp; n++) {
                buffer_i[start - (size_t)cp + (size_t)n] =
                    time_re[LTE_FFT_SIZE - cp + n];
                buffer_q[start - (size_t)cp + (size_t)n] =
                    time_im[LTE_FFT_SIZE - cp + n];
            }
        }
    }

    for (k = 0; k < BUFFER_SAMPLES; k++) {
        double phase = 2.0 * M_PI * offset_hz * (double)k / LTE_SAMPLE_RATE_HZ;
        double cr = cos(phase), ci = sin(phase);
        double ir = buffer_i[k], qi = buffer_q[k];
        buffer_i[k] = (float)(ir * cr - qi * ci + sigma * rng_normal());
        buffer_q[k] = (float)(ir * ci + qi * cr + sigma * rng_normal());
    }
}


/* ------------------------------------------------------------------ */
/* The checks.                                                         */
/* ------------------------------------------------------------------ */

static void test_bands(void) {
    uint32_t hz = 0;
    int i;

    check_int("band 20 lowest EARFCN", lte_earfcn_downlink_hz(6150, &hz), 1);
    check_int("band 20 lowest carrier", (long)hz, 791000000L);
    lte_earfcn_downlink_hz(6300, &hz);
    check_int("EARFCN 6300", (long)hz, 806000000L);
    lte_earfcn_downlink_hz(6449, &hz);
    check_int("band 20 highest carrier", (long)hz, 820900000L);
    lte_earfcn_downlink_hz(3450, &hz);
    check_int("band 8 lowest carrier", (long)hz, 925000000L);
    lte_earfcn_downlink_hz(27210, &hz);
    check_int("band 28 lowest carrier", (long)hz, 758000000L);

    check_int("EARFCN nobody claims", lte_earfcn_downlink_hz(700, &hz), 0);
    check_true("no band for an unclaimed EARFCN",
               lte_band_for_earfcn(700) == NULL);
    check_int("band of EARFCN 6300", lte_band_for_earfcn(6300)->band, 20);

    /* Every channel of every band survives the round trip, and the raster is
       what it claims to be: one hundred kilohertz, no gaps, no overlaps. */
    for (i = 0; i < lte_band_count(); i++) {
        const struct lte_band *band = lte_band_at(i);
        unsigned int earfcn;
        int round_trips = 1, spacing_holds = 1;
        uint32_t previous = 0;
        for (earfcn = band->earfcn_low; earfcn <= band->earfcn_high; earfcn++) {
            uint32_t f = 0;
            if (!lte_earfcn_downlink_hz(earfcn, &f))
                round_trips = 0;
            if (lte_earfcn_for_hz((double)f) != (int)earfcn)
                round_trips = 0;
            if (earfcn > band->earfcn_low && f - previous != 100000u)
                spacing_holds = 0;
            previous = f;
        }
        check_msg(round_trips, "band %d round trips\n", band->band);
        check_msg(spacing_holds, "band %d raster is 100 kHz\n", band->band);
    }

    /* Between two channels, the nearer one wins. */
    check_int("40 kHz above 6300", lte_earfcn_for_hz(806040000.0), 6300);
    check_int("60 kHz above 6300", lte_earfcn_for_hz(806060000.0), 6301);
    check_int("far outside every band", lte_earfcn_for_hz(1090000000.0), 0);
}

static void test_subcarrier_bin(void) {
    /* LTE never transmits on the centre subcarrier, so the bin either side of
       zero is the first one used. */
    check_int("subcarrier +1", lte_subcarrier_bin(1), 1);
    check_int("subcarrier -1", lte_subcarrier_bin(-1), 127);
    check_int("subcarrier +31", lte_subcarrier_bin(31), 31);
    check_int("subcarrier -31", lte_subcarrier_bin(-31), 97);
    check_int("subcarrier +36", lte_subcarrier_bin(36), 36);
    check_int("subcarrier -36", lte_subcarrier_bin(-36), 92);
}

static void test_fft_matches_dft(void) {
    float re[LTE_FFT_SIZE], im[LTE_FFT_SIZE];
    float back_re[LTE_FFT_SIZE], back_im[LTE_FFT_SIZE];
    float time_re[LTE_FFT_SIZE], time_im[LTE_FFT_SIZE];
    double worst = 0.0;
    int n;

    rng_seed(20260902u);
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        re[n] = (float)(rng_uniform() * 2.0 - 1.0);
        im[n] = (float)(rng_uniform() * 2.0 - 1.0);
    }
    /* Straight through the quadratic inverse transform and back out through
       the plugin's radix-2 forward one: two independent pieces of code, so
       agreement is evidence rather than tautology. */
    idft(re, im, time_re, time_im);
    lte_symbol_fft(time_re, time_im, back_re, back_im);
    for (n = 0; n < LTE_FFT_SIZE; n++) {
        double dr = fabs((double)back_re[n] - re[n]);
        double di = fabs((double)back_im[n] - im[n]);
        if (dr > worst) worst = dr;
        if (di > worst) worst = di;
    }
    check_msg(worst < 2e-4, "FFT inverts the DFT, worst error %.2e\n", worst);
}

static void test_pss_sequence(void) {
    float re[LTE_N_ID_2_COUNT][LTE_SYNC_SUBCARRIERS];
    float im[LTE_N_ID_2_COUNT][LTE_SYNC_SUBCARRIERS];
    int r, n, unit = 1;
    double worst_cross = 0.0;

    for (r = 0; r < LTE_N_ID_2_COUNT; r++)
        lte_pss_sequence(r, re[r], im[r]);

    for (r = 0; r < LTE_N_ID_2_COUNT; r++)
        for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++) {
            double magnitude = sqrt((double)re[r][n] * re[r][n] +
                                    (double)im[r][n] * im[r][n]);
            if (fabs(magnitude - 1.0) > 1e-5)
                unit = 0;
        }
    check_true("every PSS element has unit magnitude", unit);
    /* The first element of a Zadoff-Chu sequence is its own zero phase. */
    check_close("PSS 0 starts at one", re[0][0], 1.0, 1e-5);
    check_close("PSS 0 starts with no quadrature", im[0][0], 0.0, 1e-5);

    /* Different roots are what makes N_ID_2 recoverable: their correlation has
       to be far below the 62 a sequence scores against itself. */
    for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
        int s;
        for (s = r + 1; s < LTE_N_ID_2_COUNT; s++) {
            double cr = 0.0, ci = 0.0, magnitude;
            for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++) {
                cr += (double)re[r][n] * re[s][n] + (double)im[r][n] * im[s][n];
                ci += (double)im[r][n] * re[s][n] - (double)re[r][n] * im[s][n];
            }
            magnitude = sqrt(cr * cr + ci * ci);
            if (magnitude > worst_cross)
                worst_cross = magnitude;
        }
    }
    /* Not orthogonal, and they never were: puncturing the middle element of
       a length-63 Zadoff-Chu sequence costs the perfect cross-correlation the
       full-length one would have, and roots 29 and 34 are each other's
       conjugate besides. The measured worst is 23.8 of a possible 62. What
       matters is the gap, not orthogonality. */
    check_msg(worst_cross < 26.0,
              "PSS roots stay apart, worst cross-correlation %.2f of 62\n",
              worst_cross);
}

static void test_sss_sequences(void) {
    static float sequences[2 * LTE_N_ID_1_COUNT][LTE_SYNC_SUBCARRIERS];
    int n_id_1, half, n, binary = 1, duplicates = 0;
    int a, b;

    /* One N_ID_2 is enough to prove the construction; the identity only enters
       through the two scrambling sequences, which the frame test exercises. */
    for (n_id_1 = 0; n_id_1 < LTE_N_ID_1_COUNT; n_id_1++)
        for (half = 0; half < 2; half++)
            lte_sss_sequence(n_id_1, 0, half, sequences[2 * n_id_1 + half]);

    for (a = 0; a < 2 * LTE_N_ID_1_COUNT; a++)
        for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++)
            if (sequences[a][n] != 1.0f && sequences[a][n] != -1.0f)
                binary = 0;
    check_true("every SSS element is plus or minus one", binary);

    /* 336 sequences, all different: this is the whole basis for reading a cell
       identity and a half-frame off one symbol. */
    for (a = 0; a < 2 * LTE_N_ID_1_COUNT; a++)
        for (b = a + 1; b < 2 * LTE_N_ID_1_COUNT; b++) {
            int same = 1;
            for (n = 0; n < LTE_SYNC_SUBCARRIERS; n++)
                if (sequences[a][n] != sequences[b][n]) {
                    same = 0;
                    break;
                }
            if (same)
                duplicates++;
        }
    check_int("no two SSS sequences are the same", duplicates, 0);

    /* The two halves of a frame differ by a swap, not by a new sequence: the
       even elements of subframe 5 are the odd elements of subframe 0 with a
       different scrambling, which is why a receiver can tell them apart. */
    check_true("the two half-frames differ",
               memcmp(sequences[0], sequences[1], sizeof(sequences[0])) != 0);
}

static void test_gold_sequence(void) {
    uint8_t a[400], b[400];
    int n, ones = 0, differs = 0;

    lte_gold_sequence(0, 400, a);
    lte_gold_sequence(0, 400, b);
    check_int("the same seed gives the same sequence",
              memcmp(a, b, sizeof(a)), 0);

    lte_gold_sequence(12345, 400, b);
    for (n = 0; n < 400; n++)
        if (a[n] != b[n])
            differs++;
    check_msg(differs > 150 && differs < 250,
              "a different seed gives a different sequence, %d of 400 bits\n",
              differs);

    for (n = 0; n < 400; n++) {
        check_msg(a[n] <= 1, "gold bit %d is binary\n", n);
        ones += a[n];
    }
    check_msg(ones > 160 && ones < 240,
              "the sequence is balanced, %d ones of 400\n", ones);
}

static void test_crs(void) {
    float re[LTE_PBCH_SUBCARRIERS / 6], im[LTE_PBCH_SUBCARRIERS / 6];
    int positions[LTE_PBCH_SUBCARRIERS / 6];
    int pci = 227;   /* 227 mod 6 = 5, so the shift is not zero */
    int count, m, unit = 1, spaced = 1;

    count = lte_crs_sequence(pci, 1, 0, 0, 0, re, im);
    check_int("twelve reference signals across the broadcast channel",
              count, 12);
    for (m = 0; m < count; m++) {
        double magnitude = sqrt((double)re[m] * re[m] + (double)im[m] * im[m]);
        if (fabs(magnitude - 1.0) > 1e-5)
            unit = 0;
    }
    check_true("every reference signal has unit magnitude", unit);

    count = lte_crs_subcarriers(pci, 1, 0, 0, positions);
    check_int("twelve reference positions", count, 12);
    check_int("port 0 starts at the cell's own shift", positions[0], pci % 6);
    for (m = 1; m < count; m++)
        if (positions[m] - positions[m - 1] != 6)
            spaced = 0;
    check_true("every sixth subcarrier", spaced);

    lte_crs_subcarriers(pci, 1, 0, 1, positions);
    check_int("port 1 sits three subcarriers from port 0", positions[0],
              (pci % 6 + 3) % 6);
    lte_crs_subcarriers(pci, 1, 1, 2, positions);
    check_int("port 2 is in the next symbol", positions[0], (pci % 6 + 3) % 6);
    lte_crs_subcarriers(pci, 1, 1, 3, positions);
    check_int("port 3 shares port 0's shift", positions[0], pci % 6);

    /* Ports 0 and 1 come round again later in the slot with their shifts
       swapped, which is what gives the frequency refinement two readings far
       enough apart to be worth comparing. */
    lte_crs_subcarriers(pci, 1, 4, 0, positions);
    check_int("port 0 swaps shift at symbol 4", positions[0],
              (pci % 6 + 3) % 6);
    lte_crs_subcarriers(pci, 1, 4, 1, positions);
    check_int("port 1 swaps the other way", positions[0], pci % 6);
    {
        float later_re[12], later_im[12];
        lte_crs_sequence(pci, 1, 4, 0, 0, later_re, later_im);
        check_true("and carries a different sequence there",
                   memcmp(re, later_re, sizeof(re)) != 0);
    }

    /* A port is absent from the symbols that are not its own. */
    check_int("port 0 has nothing in the second symbol",
              lte_crs_subcarriers(pci, 1, 1, 0, positions), 0);
    check_int("port 2 has nothing in the first",
              lte_crs_subcarriers(pci, 1, 0, 2, positions), 0);
    check_int("nothing at all in symbol 3",
              lte_crs_subcarriers(pci, 1, 3, 0, positions), 0);

    /* The sequence is the cell's, so two cells differ. */
    {
        float other_re[12], other_im[12];
        lte_crs_sequence(pci + 1, 1, 0, 0, 0, other_re, other_im);
        check_true("a different cell has different reference signals",
                   memcmp(re, other_re, sizeof(re)) != 0);
    }
}

/* The plugin's answer for one built frame, so several cases can assert on it
   without repeating the arithmetic. */
static void search_and_check(const char *label, int pci, int ports,
                             double offset_hz, size_t skip,
                             size_t expected_subframe0, int expected_half) {
    struct lte_cell cell;
    int found;

    found = lte_cell_search(buffer_i + skip, buffer_q + skip,
                            BUFFER_SAMPLES - skip, LTE_SAMPLE_RATE_HZ, &cell, NULL);
    check_msg(found == 1, "%s: a cell is found\n", label);
    if (found != 1)
        return;
    check_msg(cell.pci == pci, "%s: cell identity %d, expected %d\n", label,
              cell.pci, pci);
    check_msg(cell.n_id_2 == pci % 3, "%s: N_ID_2 %d, expected %d\n", label,
              cell.n_id_2, pci % 3);
    check_msg(cell.n_id_1 == pci / 3, "%s: N_ID_1 %d, expected %d\n", label,
              cell.n_id_1, pci / 3);
    check_msg(!cell.extended_cp, "%s: the normal cyclic prefix is found\n",
              label);
    check_msg(cell.half_frame == expected_half,
              "%s: half-frame %d, expected %d\n", label, cell.half_frame,
              expected_half);
    check_msg(cell.subframe0_start == expected_subframe0,
              "%s: subframe 0 at %zu, expected %zu\n", label,
              cell.subframe0_start, expected_subframe0);
    check_msg(fabs(cell.frequency_offset_hz - offset_hz) < 50.0,
              "%s: offset %.0f Hz, expected %.0f\n", label,
              cell.frequency_offset_hz, offset_hz);
    check_msg(cell.pss_correlation > 0.5f,
              "%s: PSS correlation %.2f\n", label, cell.pss_correlation);
    check_msg(cell.sss_correlation > 0.7f,
              "%s: SSS correlation %.2f\n", label, cell.sss_correlation);
    check_msg(cell.sss_correlation - cell.sss_runner_up > 0.3f,
              "%s: SSS beats its runner-up, %.2f against %.2f\n", label,
              cell.sss_correlation, cell.sss_runner_up);
    /*
     * Both cyclic-prefix hypotheses are measured, not just the winner.
     *
     * This is the regression that matters: the search stops at the first
     * prefix clearing LTE_SSS_CONFIDENT, and these buffers are clean enough
     * that the normal one always does -- so before the extra read was added,
     * cp_measured[1] was 0 here and the chain still printed "normal CP" as
     * though it had compared them. A cell that scores well is exactly the
     * cell whose losing hypothesis goes unasked.
     */
    check_msg(cell.cp_measured[0] && cell.cp_measured[1],
              "%s: both cyclic prefixes measured (normal %d, extended %d)\n",
              label, cell.cp_measured[0], cell.cp_measured[1]);
    check_msg(cell.sss_correlation >= LTE_SSS_CONFIDENT,
              "%s: SSS %.2f clears the gate that ends the search early, so "
              "the extended hypothesis is one the loop skipped\n", label,
              cell.sss_correlation);
    check_msg(cell.cp_score[0] > cell.cp_score[1],
              "%s: the normal prefix outscores the extended one, "
              "%.2f against %.2f\n", label, cell.cp_score[0],
              cell.cp_score[1]);
    /* The verdict and the numbers behind it cannot disagree. */
    check_msg(cell.extended_cp == (cell.cp_score[1] > cell.cp_score[0]),
              "%s: the prefix reported matches the one that scored better\n",
              label);
    /*
     * The frame is measured from the primary sequence's peak, so a competitor
     * a whole symbol boundary away must not be close to it. On a synthetic
     * buffer there is nothing for it to compete with.
     */
    check_msg(cell.timing_sidelobe < cell.pss_correlation,
              "%s: the timing peak %.2f beats its sidelobe %.2f\n", label,
              cell.pss_correlation, cell.timing_sidelobe);
    check_msg(cell.timing_shift > -LTE_TIMING_SEARCH &&
              cell.timing_shift < LTE_TIMING_SEARCH,
              "%s: the timing moved %+d samples, inside the search window\n",
              label, cell.timing_shift);
    (void)ports;
}

static void test_cell_search(void) {
    /* Three identities chosen so all three N_ID_2 values and three different
       reference-signal shifts are exercised. */
    build_buffer(0, 1, 0.0, 0.006, 11u);
    search_and_check("cell 0", 0, 1, 0.0, 0, LEAD_IN, 0);

    build_buffer(227, 1, 2000.0, 0.006, 12u);
    search_and_check("cell 227 at +2 kHz", 227, 1, 2000.0, 0, LEAD_IN, 0);

    build_buffer(503, 2, -3500.0, 0.006, 13u);
    search_and_check("cell 503 at -3.5 kHz", 503, 2, -3500.0, 0, LEAD_IN, 0);
}

static void test_cell_search_from_the_second_half_frame(void) {
    /*
     * Start reading part-way through, so the first synchronisation signal in
     * the search window is the one halfway through a frame rather than the one
     * at its start. The secondary sequence is the only thing that says so, and
     * getting it wrong puts the frame boundary five subframes out -- which
     * would point the broadcast channel at empty air.
     */
    size_t skip = LEAD_IN + 2000;
    size_t expected = (size_t)LTE_FRAME_SAMPLES - 2000;

    build_buffer(310, 1, 1200.0, 0.006, 14u);
    search_and_check("cell 310 found in the second half-frame", 310, 1,
                     1200.0, skip, expected, 1);
}

static void test_cell_search_refuses(void) {
    struct lte_cell cell;
    struct lte_pss_result pss;
    size_t k;

    build_buffer(101, 1, 0.0, 0.006, 15u);
    check_int("a sample rate that is not LTE's is refused",
              lte_pss_detect(buffer_i, buffer_q, BUFFER_SAMPLES, 2000000.0,
                             &pss, NULL), -1);
    check_int("a block shorter than a symbol is refused",
              lte_pss_detect(buffer_i, buffer_q, 100, LTE_SAMPLE_RATE_HZ,
                             &pss, NULL), -1);

    rng_seed(99u);
    for (k = 0; k < BUFFER_SAMPLES; k++) {
        buffer_i[k] = (float)(0.05 * rng_normal());
        buffer_q[k] = (float)(0.05 * rng_normal());
    }
    check_int("noise alone is not a cell",
              lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                              LTE_SAMPLE_RATE_HZ, &cell, NULL), 0);
    check_int("and nothing is claimed about it", cell.detected, 0);
}

static void check_broadcast_bits(const char *label, int pci, int ports) {
    struct lte_cell cell;
    float soft[LTE_PBCH_SOFT_BITS];
    int written, n, wrong = 0;

    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1) {
        check_msg(0, "%s: no cell to read the broadcast channel from\n", label);
        return;
    }
    check_msg(cell.pci == pci, "%s: cell identity %d\n", label, cell.pci);

    written = lte_pbch_soft_bits(buffer_i, buffer_q, BUFFER_SAMPLES,
                                 LTE_SAMPLE_RATE_HZ, &cell,
                                 cell.subframe0_start, ports, soft, NULL);
    check_msg(written == LTE_PBCH_SOFT_BITS,
              "%s: %d soft bits, expected %d\n", label, written,
              LTE_PBCH_SOFT_BITS);
    if (written != LTE_PBCH_SOFT_BITS)
        return;

    for (n = 0; n < LTE_PBCH_SOFT_BITS; n++) {
        int decided = soft[n] > 0.0f ? 0 : 1;
        if (decided != (int)pbch_bits[n])
            wrong++;
    }
    check_msg(wrong == 0, "%s: %d of %d broadcast bits wrong\n", label, wrong,
              LTE_PBCH_SOFT_BITS);
}

static void test_broadcast_channel(void) {
    /*
     * One transmit antenna, then two and four. The port count changes how the
     * elements are combined, not where they are: a cell with four antennas
     * leaves the same holes in the grid as one with a single antenna, which is
     * what lets the count itself stay unknown until the message's own parity
     * settles it.
     */
    build_buffer(227, 1, 900.0, 0.004, 21u);
    check_broadcast_bits("one antenna port", 227, 1);

    build_buffer(227, 2, 900.0, 0.004, 22u);
    check_broadcast_bits("two antenna ports", 227, 2);

    build_buffer(101, 4, -1500.0, 0.004, 23u);
    check_broadcast_bits("four antenna ports", 101, 4);
}

static void test_broadcast_channel_refuses(void) {
    struct lte_cell cell;
    float soft[LTE_PBCH_SOFT_BITS];

    build_buffer(227, 1, 0.0, 0.004, 31u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1)
        return;

    check_int("three antenna ports is not a thing",
              lte_pbch_soft_bits(buffer_i, buffer_q, BUFFER_SAMPLES,
                                 LTE_SAMPLE_RATE_HZ, &cell,
                                 cell.subframe0_start, 3, soft, NULL), 0);
    check_int("a subframe that runs past the block is refused",
              lte_pbch_soft_bits(buffer_i, buffer_q, BUFFER_SAMPLES,
                                 LTE_SAMPLE_RATE_HZ, &cell,
                                 BUFFER_SAMPLES - 100, 1, soft, NULL), 0);

    /* The extended prefix shortens the broadcast channel to 432 bits and puts
       a third reference symbol inside it. The plugin says so rather than
       reading it wrong. */
    cell.extended_cp = 1;
    check_int("the extended cyclic prefix is declined, not misread",
              lte_pbch_soft_bits(buffer_i, buffer_q, BUFFER_SAMPLES,
                                 LTE_SAMPLE_RATE_HZ, &cell,
                                 cell.subframe0_start, 1, soft, NULL), 0);
}



/* ------------------------------------------------------------------ */
/* And the same thing off the air.                                     */
/* ------------------------------------------------------------------ */

/*
 * Everything above is a signal this file built, and that is its weakness: a
 * generator and a detector that share a mistake agree perfectly. Twice they
 * did, and both times only live air said so.
 *
 * The first was a conjugated primary sequence. The second was the "fix" for
 * it, which flipped the sign of the exponent -- wrongly, as 36.211 and
 * srsRAN's pss.c both say -- and made a broken secondary detector agree with
 * a newly broken primary one. What was actually missing was the whole-
 * subcarrier part of the tuning error: the primary sequence measures a phase,
 * a phase wraps every subcarrier, and an uncalibrated dongle is two
 * subcarriers out at 800 MHz. Every synthetic check passed throughout both.
 *
 * So the assertions here are the ones only real air can make, and each is
 * chosen because a plausible fault fails it:
 *
 *   the identity          a conjugated primary sequence gets N_ID_2 wrong
 *   the integer offset    without it the secondary sequence reads the wrong
 *                         subcarriers and returns a confident wrong answer
 *   the message           and only correct timing gets that far
 *   the frame number      which must advance at the rate the block length
 *                         implies -- the one thing chance cannot fake
 */
#define LTE_REAL_BLOCK (2 * 131072)

static void check_real_capture(const char *path, int pci, int integer_offset,
                               int bandwidth_prb, int antenna_ports,
                               int min_blocks) {
    FILE *file = fopen(path, "rb");
    unsigned char *raw;
    float *i, *q;
    int blocks = 0, cells = 0, agreed = 0, prefix = 0, offset_ok = 0;
    int messages = 0, described = 0, advanced = 0, steps = 0;
    int previous_sfn = -1;
    size_t got;

    if (!file) {
        printf("  (skipping the real-capture check: %s absent)\n", path);
        return;
    }
    raw = malloc(LTE_REAL_BLOCK);
    i = malloc((LTE_REAL_BLOCK / 2) * sizeof(*i));
    q = malloc((LTE_REAL_BLOCK / 2) * sizeof(*q));
    if (!raw || !i || !q) {
        fprintf(stderr, "real-capture allocation failed\n");
        exit(2);
    }

    while ((got = fread(raw, 1, LTE_REAL_BLOCK, file)) == LTE_REAL_BLOCK) {
        struct lte_cell cell;
        size_t pairs = LTE_REAL_BLOCK / 2, n;
        int h;
        for (n = 0; n < pairs; n++) {
            i[n] = ((float)raw[2 * n] - 127.5f) / 127.5f;
            q[n] = ((float)raw[2 * n + 1] - 127.5f) / 127.5f;
        }
        blocks++;
        if (lte_cell_search(i, q, pairs, LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1)
            continue;
        cells++;
        if (cell.pci == pci)
            agreed++;
        if (!cell.extended_cp)
            prefix++;
        if (cell.integer_offset == integer_offset)
            offset_ok++;

        for (h = 0; h < 3; h++) {
            static const int ports[3] = { 1, 2, 4 };
            float soft[LTE_PBCH_SOFT_BITS];
            struct lte_mib mib;
            if (lte_pbch_soft_bits(i, q, pairs, LTE_SAMPLE_RATE_HZ, &cell,
                                   cell.subframe0_start, ports[h], soft,
                                   NULL) != LTE_PBCH_SOFT_BITS)
                continue;
            if (!lte_mib_decode(soft, cell.pci, &mib))
                continue;
            messages++;
            if (mib.bandwidth_prb == bandwidth_prb &&
                mib.antenna_ports == antenna_ports)
                described++;
            /*
             * One block covers 131072 samples, which is 6.83 frames, so the
             * frame number advances by six or seven and by nothing else. It
             * is the assertion worth having: a bandwidth can be guessed, a
             * clock that keeps time cannot.
             */
            if (previous_sfn >= 0) {
                int step = (mib.system_frame_number - previous_sfn + 1024) %
                           1024;
                steps++;
                if (step == 6 || step == 7)
                    advanced++;
            }
            previous_sfn = mib.system_frame_number;
            break;
        }
    }
    free(raw); free(i); free(q);
    fclose(file);

    check_msg(blocks >= min_blocks, "%s holds %d blocks, expected %d\n", path,
              blocks, min_blocks);
    /* 29 of 30 as measured; the floor sits one under so a real loss of
       sensitivity shows and one marginal block does not. */
    check_msg(cells >= blocks - 1, "%s: a cell in %d of %d blocks\n", path,
              cells, blocks);
    check_msg(agreed == cells, "%s: cell %d in %d of the %d found\n", path,
              pci, agreed, cells);
    check_msg(prefix == cells, "%s: the normal prefix in %d of %d\n", path,
              prefix, cells);
    /* Without the whole-subcarrier search this is zero and everything above
       it is wrong, which is exactly how it went unnoticed. */
    check_msg(offset_ok == cells,
              "%s: %d whole subcarriers of tuning error in %d of %d\n", path,
              integer_offset, offset_ok, cells);
    check_msg(messages >= blocks - 2,
              "%s: a broadcast message in %d of %d blocks\n", path, messages,
              blocks);
    check_msg(described == messages,
              "%s: %d blocks and %d ports in %d of the %d messages\n", path,
              bandwidth_prb, antenna_ports, described, messages);
    check_msg(steps > 8 && advanced >= steps - 1,
              "%s: the frame number advanced correctly %d times of %d\n",
              path, advanced, steps);
}

/*
 * Reference signal power, and the one property that makes it worth having
 * without a calibrated receiver.
 *
 * RSRP is measured in the converter's full-scale units, so it moves with any
 * gain in front of it and is only comparable between cells read the same way.
 * RSRQ is N * RSRP / RSSI, two powers through the same chain, so every fixed
 * gain cancels -- and that is not an argument, it is a testable claim:
 * multiply the whole buffer by two and RSRP must rise by 6.02 dB while RSRQ
 * must not move at all.
 */
static void test_reference_power(void) {
    struct lte_cell cell;
    struct lte_reference_power quiet, loud;
    size_t n;

    build_buffer(227, 2, 0.0, 0.004, 41u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1) {
        check_true("reference power: the synthetic cell is found", 0);
        return;
    }
    if (lte_reference_power(buffer_i, buffer_q, BUFFER_SAMPLES,
                            LTE_SAMPLE_RATE_HZ, &cell, &quiet) != 1) {
        check_true("reference power: measured", 0);
        return;
    }

    check_int("reference power: blocks measured over",
              quiet.resource_blocks, LTE_PBCH_SUBCARRIERS / 12);
    check_msg(quiet.references > 0 && quiet.references % 12 == 0,
              "twelve references per frame, %d in all\n", quiet.references);
    /*
     * Six blocks hold seventy-two subcarriers and twelve of them are
     * references, so even a carrier with all its power in the references
     * reaches only 6 * (P/12) / P. RSRQ cannot exceed -3.01 dB, whatever the
     * signal, and a reading above it is an arithmetic fault rather than a
     * strong cell.
     */
    check_msg(quiet.rsrq_db <= -3.0f,
              "RSRQ %.2f dB is at or below the -3.01 dB ceiling\n",
              quiet.rsrq_db);

    for (n = 0; n < BUFFER_SAMPLES; n++) {
        buffer_i[n] *= 2.0f;
        buffer_q[n] *= 2.0f;
    }
    if (lte_reference_power(buffer_i, buffer_q, BUFFER_SAMPLES,
                            LTE_SAMPLE_RATE_HZ, &cell, &loud) != 1) {
        check_true("reference power: measured at twice the amplitude", 0);
        return;
    }

    check_close("RSRP follows the gain", loud.rsrp_dbfs,
                quiet.rsrp_dbfs + 6.0206, 0.01);
    check_close("RSSI follows the gain", loud.rssi_dbfs,
                quiet.rssi_dbfs + 6.0206, 0.01);
    /* The whole point: the ratio does not know the gain changed. */
    check_close("RSRQ does not", loud.rsrq_db, quiet.rsrq_db, 0.001);
}

/*
 * How many antennas the cell is transmitting on, read from the reference
 * phases and nothing else.
 *
 * This is the measurement that found the four-port cell on band 8 after every
 * other hypothesis had been eliminated, and the property that makes it work
 * is that a *level* cannot answer: reference symbols have unit magnitude, so
 * dividing by the expected sequence leaves the magnitude alone whether the
 * sequence was right or not. Only the phase separates a port that is
 * transmitting from one that is silent.
 *
 * Two buffers, differing only in how many ports carry references.
 */
static void test_port_coherence(void) {
    struct lte_cell cell;
    float two[LTE_PORT_COUNT], four[LTE_PORT_COUNT];
    int p;

    build_buffer(227, 2, 0.0, 0.004, 51u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1) {
        check_true("port coherence: the two-port cell is found", 0);
        return;
    }
    if (!lte_port_coherence(buffer_i, buffer_q, BUFFER_SAMPLES,
                            LTE_SAMPLE_RATE_HZ, &cell, two)) {
        check_true("port coherence: measured on the two-port cell", 0);
        return;
    }
    for (p = 0; p < 2; p++)
        check_msg(two[p] > 0.6f,
                  "two-port cell: port %d transmits, coherence %.2f\n", p,
                  (double)two[p]);
    /* The half that matters: a port with nothing on it must not look like a
       port with something on it, or the count is unreadable. */
    for (p = 2; p < LTE_PORT_COUNT; p++)
        check_msg(two[p] < 0.6f,
                  "two-port cell: port %d is silent, coherence %.2f\n", p,
                  (double)two[p]);

    build_buffer(101, 4, 0.0, 0.004, 52u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1) {
        check_true("port coherence: the four-port cell is found", 0);
        return;
    }
    if (!lte_port_coherence(buffer_i, buffer_q, BUFFER_SAMPLES,
                            LTE_SAMPLE_RATE_HZ, &cell, four)) {
        check_true("port coherence: measured on the four-port cell", 0);
        return;
    }
    for (p = 0; p < LTE_PORT_COUNT; p++)
        check_msg(four[p] > 0.6f,
                  "four-port cell: port %d transmits, coherence %.2f\n", p,
                  (double)four[p]);
}

/* 800 samples is 417 us at 1.92 MS/s: two sites, not one. */
#define TWO_CELL_OFFSET 800

static float other_i[BUFFER_SAMPLES];
static float other_q[BUFFER_SAMPLES];

/*
 * Two cells on one carrier, which is what the single-cell search cannot say.
 *
 * The identities are the pair actually measured on EARFCN 3625 -- PCI 190 and
 * PCI 402, whose N_ID_2 are 1 and 0, so each has its own Zadoff-Chu root and
 * both peaks are already in the correlation.
 *
 * Two things about the construction are deliberate and were arrived at by
 * measuring, not by choosing. The second cell is offset in time, because two
 * cells landing on the same sample is not the hard case but an unreal one --
 * their secondary sequences would occupy the same symbol and the weaker would
 * be read through the stronger. And it is only 1.4 dB down, because that is
 * where this works: a sweep from -16.5 dB to -1.4 dB finds the second cell at
 * -1.4 and nowhere below it, which `check_two_cells_needs_similar_levels`
 * below pins.
 *
 * That limit is not a disappointment, it is the measured case. The real pair
 * differ by 1.7 dB.
 */
static void build_two_cell_carrier(float amplitude) {
    int n;

    build_buffer(402, 2, 0.0, 0.004, 61u);
    for (n = 0; n < BUFFER_SAMPLES; n++) {
        other_i[n] = buffer_i[n] * amplitude;
        other_q[n] = buffer_q[n] * amplitude;
    }
    build_buffer(190, 2, 0.0, 0.004, 62u);
    for (n = BUFFER_SAMPLES - 1; n >= TWO_CELL_OFFSET; n--) {
        buffer_i[n] += other_i[n - TWO_CELL_OFFSET];
        buffer_q[n] += other_q[n - TWO_CELL_OFFSET];
    }
}

static void test_two_cells_on_one_carrier(void) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];
    int found, saw_190 = 0, saw_402 = 0, n;

    build_two_cell_carrier(0.85f);
    found = lte_cell_search_all(buffer_i, buffer_q, BUFFER_SAMPLES,
                                LTE_SAMPLE_RATE_HZ, cells,
                                LTE_MAX_CELLS_PER_CARRIER, NULL);
    check_int("two cells on one carrier: how many are found", found, 2);
    if (found != 2)
        return;
    for (n = 0; n < found; n++) {
        if (cells[n].pci == 190)
            saw_190 = 1;
        if (cells[n].pci == 402)
            saw_402 = 1;
    }
    check_true("the carrier's two identities are both reported",
               saw_190 && saw_402);
    /*
     * The *set* and not the order. Both cells transmit across the whole
     * measured bandwidth, so each one's reference power is measured through
     * the other's transmission -- at a decibel and a half apart that is
     * inside the interference, and asserting which comes first would be
     * pinning noise. The ordering is worth having on air, where cells differ
     * by more; it is not worth asserting here.
     */

    {
        struct lte_cell one;
        check_int("the single-cell search still returns one",
                  lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                                  LTE_SAMPLE_RATE_HZ, &one, NULL), 1);
        check_true("and it is one of the two",
                   one.pci == 190 || one.pci == 402);
    }
}

/*
 * Where it stops working, pinned so a change that claims to improve it has a
 * number to move.
 *
 * A cell 6.9 dB below the strongest is not recovered, and the reason is not
 * noise: its secondary sequence is read through the stronger cell's own
 * transmission, which no amount of integration removes. Getting it would mean
 * subtracting the stronger cell first, which this does not do and the header
 * says so.
 */
static void test_two_cells_needs_similar_levels(void) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];

    build_two_cell_carrier(0.45f);
    check_int("a cell 6.9 dB down is not separated",
              lte_cell_search_all(buffer_i, buffer_q, BUFFER_SAMPLES,
                                  LTE_SAMPLE_RATE_HZ, cells,
                                  LTE_MAX_CELLS_PER_CARRIER, NULL), 1);
    check_int("and the one reported is the stronger", cells[0].pci, 190);
}

/* One cell is one cell: the multi-cell path must not invent neighbours out of
   the two roots that did not match, which is the failure mode the per-root
   gates exist to prevent. */
static void test_one_cell_stays_one(void) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];

    build_buffer(227, 2, 900.0, 0.004, 63u);
    check_int("one cell on the carrier",
              lte_cell_search_all(buffer_i, buffer_q, BUFFER_SAMPLES,
                                  LTE_SAMPLE_RATE_HZ, cells,
                                  LTE_MAX_CELLS_PER_CARRIER, NULL), 1);
    check_int("and it is the one that was built", cells[0].pci, 227);
}

/*
 * RS-SINR, checked against noise that was put there on purpose.
 *
 * A single value proves nothing here -- any arithmetic returns one. What
 * makes this a measurement is that it *tracks*: `build_buffer` takes the
 * noise amplitude, so quadrupling it must cost twelve decibels, and the
 * reference power must not move while it does, because the signal did not.
 *
 * Measured across sigma 0.001 to 0.016, the estimate falls 32.7, 27.3, 20.4,
 * 15.1, 9.0 dB against a 24 dB rise in noise -- linear over the range that
 * matters.
 */
static void test_reference_sinr(void) {
    struct lte_cell cell;
    struct lte_reference_power quiet, noisy;

    build_buffer(227, 2, 0.0, 0.001, 71u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1 ||
        !lte_reference_power(buffer_i, buffer_q, BUFFER_SAMPLES,
                             LTE_SAMPLE_RATE_HZ, &cell, &quiet)) {
        check_true("RS-SINR: the quiet cell is measured", 0);
        return;
    }
    check_msg(quiet.sinr_db > 25.0f,
              "a clean buffer reads a high RS-SINR, got %.1f dB\n",
              (double)quiet.sinr_db);

    build_buffer(227, 2, 0.0, 0.004, 72u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1 ||
        !lte_reference_power(buffer_i, buffer_q, BUFFER_SAMPLES,
                             LTE_SAMPLE_RATE_HZ, &cell, &noisy)) {
        check_true("RS-SINR: the noisy cell is measured", 0);
        return;
    }

    /* Four times the noise amplitude is twelve decibels of power, and both
       the noise term and the ratio have to move by it. */
    check_close("four times the noise raises the noise term 12 dB",
                noisy.noise_dbfs - quiet.noise_dbfs, 12.0, 2.0);
    check_close("and costs the ratio the same",
                quiet.sinr_db - noisy.sinr_db, 12.0, 2.0);
    /* The signal did not change, so the reference power must not either --
       which is what separates a noise estimate from a gain measurement. */
    check_close("while the reference power stays put", noisy.rsrp_dbfs,
                quiet.rsrp_dbfs, 1.0);
    /* Noise below signal, or the subtraction that forms the ratio is
       meaningless. */
    check_msg(quiet.noise_dbfs < quiet.rsrp_dbfs &&
                  noisy.noise_dbfs < noisy.rsrp_dbfs,
              "the noise sits below the reference power\n");
}

/*
 * The channel's delay and the drift across a slot, both checked by putting a
 * known one there.
 *
 * Neither can be asserted as a value on a synthetic buffer that has no delay
 * and no motion -- both would read zero, and so would an implementation that
 * returned zero. What makes them measurements is that a *deliberate* delay
 * and a *deliberate* frequency error come back at the size they were made.
 */
static void test_channel_shape(void) {
    struct lte_cell cell, moved;
    struct lte_channel_shape flat, delayed, drifting;
    const double sample_ns = 1e9 / LTE_SAMPLE_RATE_HZ;

    build_buffer(227, 2, 0.0, 0.002, 81u);
    if (lte_cell_search(buffer_i, buffer_q, BUFFER_SAMPLES,
                        LTE_SAMPLE_RATE_HZ, &cell, NULL) != 1 ||
        !lte_channel_shape(buffer_i, buffer_q, BUFFER_SAMPLES,
                           LTE_SAMPLE_RATE_HZ, &cell, &flat)) {
        check_true("channel shape: measured on the flat buffer", 0);
        return;
    }
    /*
     * The grid is built without delay, and the measurement still reads about
     * 0.28 of a sample -- steady at that across five identities and three
     * noise levels, so it is something systematic in how the buffer is laid
     * rather than scatter. It is not what is being asserted here: the
     * absolute figure carries that offset and the *difference* below does
     * not, which is why the delay is checked by moving the window rather
     * than by trusting a value.
     */
    check_msg(fabsf(flat.delay_ns) < 0.4f * (float)sample_ns,
              "a flat channel is within a fraction of a sample, got %.0f ns\n",
              (double)flat.delay_ns);
    check_msg(flat.delay_spread_ns < 0.5 * sample_ns,
              "a flat channel has no spread, got %.0f ns\n",
              (double)flat.delay_spread_ns);

    /*
     * Reading the symbol two samples late is exactly a two-sample delay: the
     * transform window slides and every subcarrier picks up its share of a
     * phase ramp. Nothing else about the signal changes, so the answer is
     * known to the sample.
     */
    moved = cell;
    moved.subframe0_start = cell.subframe0_start + 2;
    if (!lte_channel_shape(buffer_i, buffer_q, BUFFER_SAMPLES,
                           LTE_SAMPLE_RATE_HZ, &moved, &delayed)) {
        check_true("channel shape: measured with the window moved", 0);
        return;
    }
    check_close("two samples late reads as two samples of delay",
                fabs((double)delayed.delay_ns - (double)flat.delay_ns),
                2.0 * sample_ns, 0.4 * sample_ns);

    /*
     * And a frequency error, made by telling the measurement the wrong
     * correction. Everything it reads then rotates at the difference, which
     * is what a Doppler would do -- the two are the same phase and the header
     * says so.
     */
    moved = cell;
    moved.frequency_offset_hz = cell.frequency_offset_hz + 300.0;
    if (!lte_channel_shape(buffer_i, buffer_q, BUFFER_SAMPLES,
                           LTE_SAMPLE_RATE_HZ, &moved, &drifting)) {
        check_true("channel shape: measured with the offset moved", 0);
        return;
    }
    check_close("300 Hz of uncorrected offset reads as 300 Hz of drift",
                fabs((double)drifting.drift_hz - (double)flat.drift_hz),
                300.0, 60.0);
    /* A delay is not a drift: moving the window must not invent one. The
       trap this guards is comparing symbol 4's references against symbol 0's
       by index, which compares different subcarriers and reads the delay as
       a frequency. */
    check_close("and a delay does not read as a drift", delayed.drift_hz,
                flat.drift_hz, 60.0);
}

int main(void) {
    twiddles_init();

    test_bands();
    test_subcarrier_bin();
    test_fft_matches_dft();
    test_pss_sequence();
    test_sss_sequences();
    test_gold_sequence();
    test_crs();
    test_cell_search();
    test_cell_search_from_the_second_half_frame();
    test_cell_search_refuses();
    test_broadcast_channel();
    test_reference_power();
    test_reference_sinr();
    test_channel_shape();
    test_port_coherence();
    test_two_cells_on_one_carrier();
    test_two_cells_needs_similar_levels();
    test_one_cell_stays_one();
    test_broadcast_channel_refuses();

    /*
     * A live band 20 cell at 796.0 MHz, recorded by this program: identity
     * 28, two subcarriers of tuning error, a 10 MHz carrier on two antenna
     * ports. Every one of those is a number the code got wrong at some point
     * while every synthetic check passed.
     */
    check_real_capture("testfiles/lte_b20_pci28.bin", 28, -2, 50, 2, 14);

    /*
     * And a four-antenna-port cell, which is a different check rather than
     * another one of the same.
     *
     * Every other cell reachable from this site transmits on two ports, and
     * the port hypotheses are tried 1, 2, 4 and stop at the first that fits,
     * so a two-port cell strong enough for single-port combining never
     * reaches the transmit-diversity code at all. It was wrong for as long as
     * it existed -- the second symbol of each Alamouti pair came out as
     * -conj(x1) -- and place_broadcast() above transposed the same two terms,
     * so every synthetic check passed while band 8 gave 216 cells and zero
     * messages.
     *
     * This is the file that convention cannot satisfy, and 4 is the number
     * that matters: it is read out of the parity mask, and the reference
     * signals say four independently, by phase, with no decoding at all.
     */
    check_real_capture("testfiles/lte_b8_pci330_4port.bin", 330, -2, 25, 4, 3);

    return check_report("lte cell search and broadcast channel");
}
