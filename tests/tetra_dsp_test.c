#include "check.h"

#include "tetra_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * One carrier to dibits.
 *
 * The synthetic half is a round trip and is worth exactly what a round trip is
 * worth: it cannot see a convention both sides share, which is why the phase
 * table is checked for the properties it must have rather than for the values
 * it happens to hold, and why the mapping itself is not claimed until a parity
 * check passes over real symbols (ticket 03).
 *
 * The half that means something is the last test, which demodulates real air.
 */

#define RATE 2000000.0
#define SYNTH_SYMBOLS 900

static uint32_t seed = 0xc0ffeeu;
static double uniform(void) {
    seed = seed * 1103515245u + 12345u;
    return (double)((seed >> 8) & 0xffffffu) / 16777216.0;
}
static double gaussian(void) {
    double u = uniform(), v = uniform();
    if (u < 1e-12)
        u = 1e-12;
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

/*
 * The table, checked against what a pi/4-DQPSK table must be whatever the
 * standard's assignment happens to be.
 */
static void test_the_phase_steps(void) {
    int seen[8];
    int d, k;

    memset(seen, 0, sizeof(seen));
    for (d = 0; d < TETRA_PHASE_STEPS; d++) {
        double step = tetra_step_for_dibit(d);
        double quarters = step / (M_PI / 4.0);
        int rounded = (int)floor(quarters + 0.5);

        check_msg(fabs(quarters - rounded) < 1e-9,
                  "dibit %d sends %.4f rad, which is %.3f quarter-turns and "
                  "not a whole number of them\n", d, step, quarters);
        check_msg((rounded & 1) != 0,
                  "dibit %d sends %d quarter-turns, which is even -- pi/4 "
                  "and 3pi/4 are what make this pi/4-DQPSK rather than QPSK\n",
                  d, rounded);
        check_msg(rounded >= -4 && rounded <= 4,
                  "dibit %d sends %d quarter-turns, outside a whole turn\n", d,
                  rounded);
        seen[(rounded + 4) & 7]++;
    }
    for (k = 0; k < 8; k++)
        check_msg(seen[k] <= 1, "two dibits send the same phase step\n");

    /* And the slicer is the table's inverse, including at the boundaries
       where a rounding error would send a dibit to its neighbour. */
    for (d = 0; d < TETRA_PHASE_STEPS; d++) {
        char name[48];
        snprintf(name, sizeof(name), "dibit %d survives the slicer", d);
        check_int(name, tetra_dibit_for_step(tetra_step_for_dibit(d)), d);
        snprintf(name, sizeof(name), "dibit %d, nudged early", d);
        check_int(name,
                  tetra_dibit_for_step(tetra_step_for_dibit(d) - 0.3), d);
        snprintf(name, sizeof(name), "dibit %d, nudged late", d);
        check_int(name,
                  tetra_dibit_for_step(tetra_step_for_dibit(d) + 0.3), d);
    }
}

/*
 * A synthetic carrier: dibits to pi/4-DQPSK at 18 ksym/s, pulse-shaped, put on
 * an offset carrier at the house rate, with noise.
 */
static size_t synthesise(unsigned char *sent, int symbols, double offset_hz,
                         double timing_fraction, double noise,
                         float *out_i, float *out_q, size_t capacity) {
    double sps = RATE / TETRA_SYMBOL_RATE_HZ;
    size_t pairs = (size_t)((symbols + 2 * TETRA_RRC_SPAN) * sps);
    size_t n;
    static double phase[SYNTH_SYMBOLS + 64];
    double running = 0.0;
    int k;

    if (pairs > capacity)
        pairs = capacity;
    for (k = 0; k < symbols; k++) {
        sent[k] = (unsigned char)((int)(uniform() * 4.0) & 3);
        running += tetra_step_for_dibit(sent[k]);
        phase[k] = running;
    }
    for (n = 0; n < pairs; n++) {
        double t = (double)n / RATE;
        double re = 0.0, im = 0.0;
        double centre = t * TETRA_SYMBOL_RATE_HZ - timing_fraction;
        int first = (int)floor(centre) - TETRA_RRC_SPAN;
        int last = (int)floor(centre) + TETRA_RRC_SPAN;

        for (k = first; k <= last; k++) {
            double x, h, num, den, a = TETRA_RRC_ROLLOFF;
            if (k < 0 || k >= symbols)
                continue;
            x = centre - (double)k;
            if (fabs(x) < 1e-9) {
                h = 1.0 - a + 4.0 * a / M_PI;
            } else if (a > 0.0 && fabs(fabs(x) - 1.0 / (4.0 * a)) < 1e-9) {
                h = a / sqrt(2.0) *
                    ((1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * a)) +
                     (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * a)));
            } else {
                num = sin(M_PI * x * (1.0 - a)) +
                      4.0 * a * x * cos(M_PI * x * (1.0 + a));
                den = M_PI * x * (1.0 - (4.0 * a * x) * (4.0 * a * x));
                h = num / den;
            }
            re += h * cos(phase[k]);
            im += h * sin(phase[k]);
        }
        {
            double w = 2.0 * M_PI * offset_hz * t;
            double c = cos(w), s = sin(w);
            out_i[n] = (float)(re * c - im * s + noise * gaussian());
            out_q[n] = (float)(re * s + im * c + noise * gaussian());
        }
    }
    return pairs;
}

static float work_i[TETRA_MAX_WORK], work_q[TETRA_MAX_WORK];
static float air_i[400000], air_q[400000];

/*
 * What a clean signal actually achieves, measured rather than hoped for.
 *
 * The residual is about 0.05 radians -- 2.9 degrees on a constellation whose
 * points are 90 apart -- and it is flat across every timing phase, so it is
 * the floor of this implementation and not a timing fault: the boxcar
 * decimation's slight tilt across the channel, the matched filter's eight
 * symbols of span, the cubic interpolator, and a constant 0.006-symbol bias in
 * the timing estimate. It costs no dibits.
 *
 * 0.95 was asserted here first, on no evidence, and failed. The number that
 * matters is not this one anyway: it is the gap between a clean signal at 0.90
 * and noise at under 0.35, which is what makes `lock` able to answer whether a
 * channel carries TETRA at all.
 */
#define CLEAN_LOCK 0.85
#define NOISY_LOCK 0.60

static void round_trip(const char *what, double offset_hz,
                       double timing_fraction, double noise,
                       double expect_lock) {
    static unsigned char sent[SYNTH_SYMBOLS + 64];
    static struct tetra_symbols got;
    size_t pairs, filtered;
    char name[96];
    int wrong = 0, k, aligned = -1, best_wrong = 1 << 30;

    pairs = synthesise(sent, SYNTH_SYMBOLS, offset_hz, timing_fraction, noise,
                       air_i, air_q, sizeof(air_i) / sizeof(air_i[0]));
    filtered = tetra_channel(air_i, air_q, pairs, RATE, offset_hz, work_i,
                             work_q, TETRA_MAX_WORK);
    snprintf(name, sizeof(name), "%s: the channel filter produces samples",
             what);
    check_true(name, filtered > 1000);
    if (filtered <= 1000)
        return;

    check_true("the demodulator produces symbols",
               tetra_demodulate(work_i, work_q, filtered, offset_hz, &got) >
                   100);
    snprintf(name, sizeof(name), "%s: lock", what);
    check_msg(got.lock >= expect_lock,
              "%s: lock %.3f, wanted at least %.2f (rms error %.3f rad)\n",
              what, (double)got.lock, expect_lock,
              (double)got.rms_error_rad);

    /*
     * The dibits, allowing for where in the sent sequence the demodulator
     * happened to start -- the filter's delay is a whole number of samples but
     * not of symbols, and nothing here has yet found a burst boundary. That is
     * ticket 02's job; this only claims the *symbols* are right.
     */
    for (k = 0; k < 12; k++) {
        int offset = k, n, bad = 0;
        for (n = 0; n + offset < SYNTH_SYMBOLS && n < got.count - 1; n++)
            if (got.dibit[n + 1] != sent[n + offset])
                bad++;
        if (bad < best_wrong) {
            best_wrong = bad;
            aligned = offset;
        }
    }
    wrong = best_wrong;
    snprintf(name, sizeof(name), "%s: dibits", what);
    check_msg(wrong <= SYNTH_SYMBOLS / 50,
              "%s: %d of about %d dibits wrong at the best alignment (%d)\n",
              what, wrong, SYNTH_SYMBOLS, aligned);
}

static void test_round_trips(void) {
    /* Clean, on frequency, symbols on the sample grid. */
    round_trip("clean", 0.0, 0.0, 0.0, CLEAN_LOCK);
    /* Symbols between samples, which is the usual case at 111.11 samples per
       symbol -- there is no grid to be on. */
    round_trip("timing half a symbol out", 0.0, 0.5, 0.0, CLEAN_LOCK);
    round_trip("timing a third out", 0.0, 0.333, 0.0, CLEAN_LOCK);
    /*
     * On an offset carrier. tetra_channel removes what it is told; what is
     * left for the fine stage is what the coarse stage could not resolve.
     */
    round_trip("2 kHz off", 2000.0, 0.25, 0.0, CLEAN_LOCK);
    round_trip("13.7 kHz off, as 35 ppm at 392 MHz", 13700.0, 0.1, 0.0,
               CLEAN_LOCK);
    /* And with noise, which is what the lock figure is for. */
    round_trip("noisy", 0.0, 0.4, 0.02, NOISY_LOCK);
}

/*
 * The fine offset really is measured, not assumed: leave a residual behind
 * that tetra_channel was not told about, and the demodulator should find it.
 */
static void test_the_fine_offset(void) {
    static unsigned char sent[SYNTH_SYMBOLS + 64];
    static struct tetra_symbols got;
    size_t pairs, filtered;
    double residual = 700.0;

    pairs = synthesise(sent, SYNTH_SYMBOLS, residual, 0.2, 0.0, air_i, air_q,
                       sizeof(air_i) / sizeof(air_i[0]));
    /* Told nothing, so the whole 700 Hz is left for the fine stage. */
    filtered = tetra_channel(air_i, air_q, pairs, RATE, 0.0, work_i, work_q,
                             TETRA_MAX_WORK);
    tetra_demodulate(work_i, work_q, filtered, 0.0, &got);
    check_close("the residual offset is measured", got.fine_offset_hz,
                residual, 60.0);
    check_msg(got.lock > CLEAN_LOCK,
              "with the offset removed the constellation should be clean, "
              "lock %.3f\n", (double)got.lock);
}

/* A sample rate that is not a whole multiple of the working rate is refused
   rather than resampled, the way lte_ refuses anything but its own grid. */
static void test_it_refuses_a_rate_it_cannot_use(void) {
    check_size("2 MS/s is twenty times the working rate",
               tetra_channel(air_i, air_q, 4096, RATE, 0.0, work_i, work_q,
                             TETRA_MAX_WORK) > 0,
               1);
    check_size("2.048 MS/s is not a whole multiple",
               tetra_channel(air_i, air_q, 4096, 2048000.0, 0.0, work_i,
                             work_q, TETRA_MAX_WORK),
               0);
    check_size("nor is 1.92",
               tetra_channel(air_i, air_q, 4096, 1920000.0, 0.0, work_i,
                             work_q, TETRA_MAX_WORK),
               0);
}

/*
 * Noise is not a constellation.
 *
 * The lock figure exists to say whether this is TETRA at all, so it has to
 * come out near zero for something that is not -- otherwise every empty
 * channel reports a clean decode.
 */
static void test_noise_does_not_lock(void) {
    static struct tetra_symbols got;
    size_t n, filtered;

    for (n = 0; n < 200000; n++) {
        air_i[n] = (float)gaussian();
        air_q[n] = (float)gaussian();
    }
    filtered = tetra_channel(air_i, air_q, 200000, RATE, 0.0, work_i, work_q,
                             TETRA_MAX_WORK);
    tetra_demodulate(work_i, work_q, filtered, 0.0, &got);
    check_msg(got.lock < 0.35,
              "pure noise locked at %.3f, so the figure cannot be used to say "
              "whether a channel carries TETRA\n", (double)got.lock);
}

/*
 * The burst grid, found from the symbols rather than from a transcribed
 * training sequence.
 *
 * The synthetic case is a stream built the way a burst is: some positions the
 * same every period, the rest random. That is the *shape* the finder looks
 * for, and it is checkable without knowing a single bit of any real sequence.
 */
static void test_the_burst_grid(void) {
    static unsigned char stream[40000];
    static struct tetra_burst_sync sync;
    const int period = TETRA_SLOT_SYMBOLS;
    static unsigned char fixed_part[TETRA_SLOT_SYMBOLS];
    static int is_fixed[TETRA_SLOT_SYMBOLS];
    int k, n, fixed_wanted = 0;

    for (k = 0; k < period; k++) {
        fixed_part[k] = (unsigned char)((int)(uniform() * 4.0) & 3);
        /* Roughly two positions in three fixed, which is close to what the
           real capture turned out to be: 181 of 255. */
        is_fixed[k] = uniform() < 0.7;
        if (is_fixed[k])
            fixed_wanted++;
    }
    for (n = 0; n < 40000; n++) {
        int phase = n % period;
        stream[n] = is_fixed[phase] ? fixed_part[phase]
                                    : (unsigned char)((int)(uniform() * 4.0) & 3);
    }

    check_true("a burst grid is found",
               tetra_burst_find(stream, 40000, 200, 1200, &sync) == 1);
    check_int("at the right period", sync.period, period);
    check_msg(sync.repeat > sync.runner_up * 1.4f,
              "the period stands at %.3f against a runner-up of %.3f, which "
              "is not standing clear\n", (double)sync.repeat,
              (double)sync.runner_up);
    /* Fixed positions plus a quarter of the rest: that is what the overall
       match must come to, and it is arithmetic rather than a fitted number. */
    {
        double expected = (double)fixed_wanted / period +
                          0.25 * (1.0 - (double)fixed_wanted / period);
        check_close("the match is the fixed fraction plus chance on the rest",
                    sync.repeat, expected, 0.05);
    }
    check_msg(sync.fixed >= fixed_wanted - 8 && sync.fixed <= fixed_wanted + 8,
              "found %d fixed positions, built %d\n", sync.fixed,
              fixed_wanted);
    check_true("and the rest look like data", sync.varying > 30);
}

/*
 * Noise has no burst grid, and this is the half that matters: a finder that
 * reports the best of a thousand lags always reports something.
 */
static void test_noise_has_no_grid(void) {
    static unsigned char stream[40000];
    static struct tetra_burst_sync sync;
    int n;

    for (n = 0; n < 40000; n++)
        stream[n] = (unsigned char)((int)(uniform() * 4.0) & 3);
    check_int("noise has no period", tetra_burst_find(stream, 40000, 200, 1200,
                                                      &sync), 0);
    check_msg(sync.repeat < 0.30f,
              "noise matched itself at %.3f, which should be near a quarter\n",
              (double)sync.repeat);

    /* And a stream that is entirely fixed is not a burst structure either --
       it is a stuck receiver, and the profile says so. */
    for (n = 0; n < 40000; n++)
        stream[n] = (unsigned char)(n % TETRA_SLOT_SYMBOLS % 4);
    if (tetra_burst_find(stream, 40000, 200, 1200, &sync)) {
        check_msg(sync.varying == 0,
                  "a wholly repeating stream should have no varying "
                  "positions, found %d\n", sync.varying);
    }
}

int main(void) {
    test_the_phase_steps();
    test_round_trips();
    test_the_fine_offset();
    test_it_refuses_a_rate_it_cannot_use();
    test_noise_does_not_lock();
    test_the_burst_grid();
    test_noise_has_no_grid();

    return check_report("TETRA: a carrier to symbols");
}
