#include "lte_turbo.h"

#include <string.h>

/*
 * The interleaver's parameters, 36.212 Table 5.1.3-3.
 *
 * Transcribed, and therefore suspect. check-lte-turbo validates every row
 * against the property that makes a quadratic permutation polynomial a
 * permutation at all -- f1 coprime with K, and every prime factor of K
 * dividing f2 -- which is a fact about the numbers and involves no encoder of
 * ours. A mistyped f1 breaks the coprimality; a mistyped f2 breaks the
 * divisibility. Both fail loudly rather than producing an interleaver that
 * works only against itself.
 */
static const struct { int k; int f1; int f2; } qpp[LTE_TURBO_SIZES] = {
    {   40,   3,  10 }, {   48,   7,  12 }, {   56,  19,  42 }, {   64,   7,  16 },
    {   72,   7,  18 }, {   80,  11,  20 }, {   88,   5,  22 }, {   96,  11,  24 },
    {  104,   7,  26 }, {  112,  41,  84 }, {  120, 103,  90 }, {  128,  15,  32 },
    {  136,   9,  34 }, {  144,  17, 108 }, {  152,   9,  38 }, {  160,  21, 120 },
    {  168, 101,  84 }, {  176,  21,  44 }, {  184,  57,  46 }, {  192,  23,  48 },
    {  200,  13,  50 }, {  208,  27,  52 }, {  216,  11,  36 }, {  224,  27,  56 },
    {  232,  85,  58 }, {  240,  29,  60 }, {  248,  33,  62 }, {  256,  15,  32 },
    {  264,  17, 198 }, {  272,  33,  68 }, {  280, 103, 210 }, {  288,  19,  36 },
    {  296,  19,  74 }, {  304,  37,  76 }, {  312,  19,  78 }, {  320,  21, 120 },
    {  328,  21,  82 }, {  336, 115,  84 }, {  344, 193,  86 }, {  352,  21,  44 },
    {  360, 133,  90 }, {  368,  81,  46 }, {  376,  45,  94 }, {  384,  23,  48 },
    {  392, 243,  98 }, {  400, 151,  40 }, {  408, 155, 102 }, {  416,  25,  52 },
    {  424,  51, 106 }, {  432,  47,  72 }, {  440,  91, 110 }, {  448,  29, 168 },
    {  456,  29, 114 }, {  464, 247,  58 }, {  472,  29, 118 }, {  480,  89, 180 },
    {  488,  91, 122 }, {  496, 157,  62 }, {  504,  55,  84 }, {  512,  31,  64 },
    {  528,  17,  66 }, {  544,  35,  68 }, {  560, 227, 420 }, {  576,  65,  96 },
    {  592,  19,  74 }, {  608,  37,  76 }, {  624,  41, 234 }, {  640,  39,  80 },
    {  656, 185,  82 }, {  672,  43, 252 }, {  688,  21,  86 }, {  704, 155,  44 },
    {  720,  79, 120 }, {  736, 139,  92 }, {  752,  23,  94 }, {  768, 217,  48 },
    {  784,  25,  98 }, {  800,  17,  80 }, {  816, 127, 102 }, {  832,  25,  52 },
    {  848, 239, 106 }, {  864,  17,  48 }, {  880, 137, 110 }, {  896, 215, 112 },
    {  912,  29, 114 }, {  928,  15,  58 }, {  944, 147, 118 }, {  960,  29,  60 },
    {  976,  59, 122 }, {  992,  65, 124 }, { 1008,  55,  84 }, { 1024,  31,  64 },
    { 1056,  17,  66 }, { 1088, 171, 204 }, { 1120,  67, 140 }, { 1152,  35,  72 },
    { 1184,  19,  74 }, { 1216,  39,  76 }, { 1248,  19,  78 }, { 1280, 199, 240 },
    { 1312,  21,  82 }, { 1344, 211, 252 }, { 1376,  21,  86 }, { 1408,  43,  88 },
    { 1440, 149,  60 }, { 1472,  45,  92 }, { 1504,  49, 846 }, { 1536,  71,  48 },
    { 1568,  13,  28 }, { 1600,  17,  80 }, { 1632,  25, 102 }, { 1664, 183, 104 },
    { 1696,  55, 954 }, { 1728, 127,  96 }, { 1760,  27, 110 }, { 1792,  29, 112 },
    { 1824,  29, 114 }, { 1856,  57, 116 }, { 1888,  45, 354 }, { 1920,  31, 120 },
    { 1952,  59, 610 }, { 1984, 185, 124 }, { 2016, 113, 420 }, { 2048,  31,  64 },
    { 2112,  17,  66 }, { 2176, 171, 136 }, { 2240, 209, 420 }, { 2304, 253, 216 },
    { 2368, 367, 444 }, { 2432, 265, 456 }, { 2496, 181, 468 }, { 2560,  39,  80 },
    { 2624,  27, 164 }, { 2688, 127, 504 }, { 2752, 143, 172 }, { 2816,  43,  88 },
    { 2880,  29, 300 }, { 2944,  45,  92 }, { 3008, 157, 188 }, { 3072,  47,  96 },
    { 3136,  13,  28 }, { 3200, 111, 240 }, { 3264, 443, 204 }, { 3328,  51, 104 },
    { 3392,  51, 212 }, { 3456, 451, 192 }, { 3520, 257, 220 }, { 3584,  57, 336 },
    { 3648, 313, 228 }, { 3712, 271, 232 }, { 3776, 179, 236 }, { 3840, 331, 120 },
    { 3904, 363, 244 }, { 3968, 375, 248 }, { 4032, 127, 168 }, { 4096,  31,  64 },
    { 4160,  33, 130 }, { 4224,  43, 264 }, { 4288,  33, 134 }, { 4352, 477, 408 },
    { 4416,  35, 138 }, { 4480, 233, 280 }, { 4544, 357, 142 }, { 4608, 337, 480 },
    { 4672,  37, 146 }, { 4736,  71, 444 }, { 4800,  71, 120 }, { 4864,  37, 152 },
    { 4928,  39, 462 }, { 4992, 127, 234 }, { 5056,  39, 158 }, { 5120,  39,  80 },
    { 5184,  31,  96 }, { 5248, 113, 902 }, { 5312,  41, 166 }, { 5376, 251, 336 },
    { 5440,  43, 170 }, { 5504,  21,  86 }, { 5568,  43, 174 }, { 5632,  45, 176 },
    { 5696,  45, 178 }, { 5760, 161, 120 }, { 5824,  89, 182 }, { 5888, 323, 184 },
    { 5952,  47, 186 }, { 6016,  23,  94 }, { 6080,  47, 190 }, { 6144, 263, 480 },};

int lte_turbo_block_size(int index) {
    if (index < 0 || index >= LTE_TURBO_SIZES)
        return 0;
    return qpp[index].k;
}

int lte_turbo_size_index(int k) {
    int i;

    for (i = 0; i < LTE_TURBO_SIZES; i++) {
        if (qpp[i].k == k)
            return i;
        if (qpp[i].k > k)
            break;
    }
    return -1;
}

int lte_turbo_fit(int bits) {
    int i;

    for (i = 0; i < LTE_TURBO_SIZES; i++)
        if (qpp[i].k >= bits)
            return qpp[i].k;
    return 0;
}

int lte_turbo_params(int k, int *f1, int *f2) {
    int i = lte_turbo_size_index(k);

    if (i < 0)
        return 0;
    if (f1)
        *f1 = qpp[i].f1;
    if (f2)
        *f2 = qpp[i].f2;
    return 1;
}

int lte_turbo_pi(int k, int i) {
    int f1, f2;
    long long t;

    if (i < 0 || i >= k || !lte_turbo_params(k, &f1, &f2))
        return -1;
    /*
     * Reduced at every step rather than after multiplying out: f2 * i * i at
     * K = 6144 overflows 32 bits well before the modulus would bring it back.
     */
    t = ((long long)f2 * i) % k;
    t = (t * i) % k;
    t = (t + (long long)f1 * i) % k;
    return (int)t;
}

/*
 * One eight-state recursive systematic encoder, 36.212 section 5.1.3.2:
 * G(D) = [1, g1(D)/g0(D)], g0 = 1 + D^2 + D^3, g1 = 1 + D + D^3.
 *
 * The state is the three registers packed low to high, so state 0 is the zero
 * state both encoders are driven back to.
 */
#define S0(s) ((s) & 1)
#define S1(s) (((s) >> 1) & 1)
#define S2(s) (((s) >> 2) & 1)

static int rsc_next(int state, int input, int *parity) {
    int feedback = input ^ S1(state) ^ S2(state);

    *parity = feedback ^ S0(state) ^ S2(state);
    return feedback | (S0(state) << 1) | (S1(state) << 2);
}

/* The input that drives the feedback to zero, which is how the trellis is
   terminated. LTE turbo does not tail-bite; it spends three bits per encoder
   getting back to the zero state. */
static int rsc_terminating_input(int state) {
    return S1(state) ^ S2(state);
}

/* One constituent encoder over `k` bits plus its three termination steps.
   `x` and `z` are k + 3 long. */
static void rsc_encode(const uint8_t *in, int k, uint8_t *x, uint8_t *z) {
    int state = 0, i, parity;

    for (i = 0; i < k; i++) {
        x[i] = in[i];
        state = rsc_next(state, in[i], &parity);
        z[i] = (uint8_t)parity;
    }
    for (i = 0; i < 3; i++) {
        int u = rsc_terminating_input(state);
        x[k + i] = (uint8_t)u;
        state = rsc_next(state, u, &parity);
        z[k + i] = (uint8_t)parity;
    }
}

void lte_turbo_encode(const uint8_t *in, int k, uint8_t *d0, uint8_t *d1,
                      uint8_t *d2) {
    static uint8_t second[LTE_TURBO_MAX_K];
    static uint8_t x[LTE_TURBO_MAX_K + 3], z[LTE_TURBO_MAX_K + 3];
    static uint8_t xp[LTE_TURBO_MAX_K + 3], zp[LTE_TURBO_MAX_K + 3];
    int i;

    if (!in || !d0 || !d1 || !d2 || lte_turbo_size_index(k) < 0)
        return;
    for (i = 0; i < k; i++)
        second[i] = in[lte_turbo_pi(k, i)];
    rsc_encode(in, k, x, z);
    rsc_encode(second, k, xp, zp);

    for (i = 0; i < k; i++) {
        d0[i] = x[i];
        d1[i] = z[i];
        d2[i] = zp[i];
    }
    /*
     * The twelve tail bits, spread across the three streams so that no single
     * stream carries a whole encoder's termination (36.212 5.1.3.2.2). The
     * interleaving looks arbitrary and is not: it keeps the tail's protection
     * even when one stream is punctured hardest.
     */
    d0[k + 0] = x[k];      d0[k + 1] = z[k + 1];
    d0[k + 2] = xp[k];     d0[k + 3] = zp[k + 1];
    d1[k + 0] = z[k];      d1[k + 1] = x[k + 2];
    d1[k + 2] = zp[k];     d1[k + 3] = xp[k + 2];
    d2[k + 0] = x[k + 1];  d2[k + 1] = z[k + 2];
    d2[k + 2] = xp[k + 1]; d2[k + 3] = zp[k + 2];
}

/*
 * The decoder.
 *
 * Max-log-MAP: the log-domain BCJR with log(e^a + e^b) replaced by max(a, b).
 * It costs a few tenths of a decibel and buys three things that matter more
 * here than those tenths -- no lookup table, no scaling of the input, and no
 * need to know the noise variance, which nothing upstream measures.
 *
 * Soft convention as everywhere else in this repository: positive means the
 * bit is more likely 0, negative more likely 1. So a bit b contributes
 * (1 - 2b) * L / 2 to a path metric.
 */

#define NEG_INF (-1.0e30f)

static float maxf(float a, float b) { return a > b ? a : b; }

/* Precomputed trellis: for each state and input, where it goes and what it
   emits. Built once rather than per window, and small enough to sit in the
   file rather than be handed around. */
static struct { int next[2]; int parity[2]; } trellis[LTE_TURBO_STATES];
static int trellis_ready;

static void build_trellis(void) {
    int s, u, p;

    if (trellis_ready)
        return;
    for (s = 0; s < LTE_TURBO_STATES; s++)
        for (u = 0; u < 2; u++) {
            trellis[s].next[u] = rsc_next(s, u, &p);
            trellis[s].parity[u] = p;
        }
    trellis_ready = 1;
}

/*
 * One constituent decoder over n steps, terminated at the zero state.
 *
 * `sys` and `par` are the channel values for this encoder's systematic and
 * parity bits; `apriori` is what the other decoder learned, and is not fed
 * back into the output -- what comes out is *extrinsic*, the part this
 * decoder contributed, which is the whole mechanism by which two decoders
 * improve each other rather than talking themselves into agreement.
 */
static void bcjr(const float *sys, const float *par, const float *apriori,
                 int n, float *extrinsic) {
    static float alpha[LTE_TURBO_MAX_K + 4][LTE_TURBO_STATES];
    static float beta[LTE_TURBO_MAX_K + 4][LTE_TURBO_STATES];
    int i, s, u;

    build_trellis();

    for (s = 0; s < LTE_TURBO_STATES; s++) {
        alpha[0][s] = s == 0 ? 0.0f : NEG_INF;
        beta[n][s] = s == 0 ? 0.0f : NEG_INF;   /* terminated, not tail-biting */
    }

    for (i = 0; i < n; i++) {
        for (s = 0; s < LTE_TURBO_STATES; s++)
            alpha[i + 1][s] = NEG_INF;
        for (s = 0; s < LTE_TURBO_STATES; s++) {
            if (alpha[i][s] <= NEG_INF)
                continue;
            for (u = 0; u < 2; u++) {
                int ns = trellis[s].next[u];
                float g = 0.5f * ((1.0f - 2.0f * (float)u) *
                                      (sys[i] + apriori[i]) +
                                  (1.0f - 2.0f * (float)trellis[s].parity[u]) *
                                      par[i]);
                alpha[i + 1][ns] = maxf(alpha[i + 1][ns], alpha[i][s] + g);
            }
        }
    }

    for (i = n - 1; i >= 0; i--) {
        for (s = 0; s < LTE_TURBO_STATES; s++) {
            float best = NEG_INF;
            for (u = 0; u < 2; u++) {
                int ns = trellis[s].next[u];
                float g = 0.5f * ((1.0f - 2.0f * (float)u) *
                                      (sys[i] + apriori[i]) +
                                  (1.0f - 2.0f * (float)trellis[s].parity[u]) *
                                      par[i]);
                best = maxf(best, beta[i + 1][ns] + g);
            }
            beta[i][s] = best;
        }
    }

    for (i = 0; i < n; i++) {
        float best[2] = { NEG_INF, NEG_INF };
        for (s = 0; s < LTE_TURBO_STATES; s++) {
            if (alpha[i][s] <= NEG_INF)
                continue;
            for (u = 0; u < 2; u++) {
                int ns = trellis[s].next[u];
                float g = 0.5f * (1.0f - 2.0f * (float)trellis[s].parity[u]) *
                          par[i];
                float m = alpha[i][s] + g + beta[i + 1][ns];
                if (m > best[u])
                    best[u] = m;
            }
        }
        /* The parity's contribution only. The systematic value and the a
           priori are deliberately absent: adding them back is how a turbo
           decoder feeds its own opinion to its partner and stops converging
           on anything but its first guess. */
        extrinsic[i] = best[0] - best[1];
    }
}

void lte_turbo_decode(const float *d0, const float *d1, const float *d2,
                      int k, int iterations, uint8_t *out) {
    static float sys1[LTE_TURBO_MAX_K + 3], par1[LTE_TURBO_MAX_K + 3];
    static float sys2[LTE_TURBO_MAX_K + 3], par2[LTE_TURBO_MAX_K + 3];
    static float a1[LTE_TURBO_MAX_K + 3], a2[LTE_TURBO_MAX_K + 3];
    static float e1[LTE_TURBO_MAX_K + 3], e2[LTE_TURBO_MAX_K + 3];
    int i, it, n = k + 3;

    if (!d0 || !d1 || !d2 || !out || lte_turbo_size_index(k) < 0)
        return;

    for (i = 0; i < k; i++) {
        sys1[i] = d0[i];
        par1[i] = d1[i];
        sys2[i] = d0[lte_turbo_pi(k, i)];
        par2[i] = d2[i];
    }
    /* Undo the tail's spread across the three streams. */
    sys1[k] = d0[k];     par1[k] = d1[k];
    sys1[k + 1] = d2[k]; par1[k + 1] = d0[k + 1];
    sys1[k + 2] = d1[k + 1]; par1[k + 2] = d2[k + 1];
    sys2[k] = d0[k + 2];     par2[k] = d1[k + 2];
    sys2[k + 1] = d2[k + 2]; par2[k + 1] = d0[k + 3];
    sys2[k + 2] = d1[k + 3]; par2[k + 2] = d2[k + 3];

    for (i = 0; i < n; i++) {
        a1[i] = 0.0f;
        a2[i] = 0.0f;
    }

    for (it = 0; it < iterations; it++) {
        bcjr(sys1, par1, a1, n, e1);
        /* What the first decoder learned, in the second's order. The tail
           positions have no counterpart, so they start from nothing. */
        for (i = 0; i < k; i++)
            a2[i] = e1[lte_turbo_pi(k, i)];
        for (i = k; i < n; i++)
            a2[i] = 0.0f;

        bcjr(sys2, par2, a2, n, e2);
        for (i = 0; i < k; i++)
            a1[lte_turbo_pi(k, i)] = e2[i];
        for (i = k; i < n; i++)
            a1[i] = 0.0f;
    }

    for (i = 0; i < k; i++) {
        float total = sys1[i] + a1[i] + e1[i];
        out[i] = total < 0.0f ? 1 : 0;
    }
}
