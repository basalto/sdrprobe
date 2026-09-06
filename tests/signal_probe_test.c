/*
 * Where a carrier is, and whether anything is riding on it.
 */

#include <math.h>
#include <stdlib.h>

#include "check.h"
#include "signal_probe.h"

#define N 200000
static float ir[N], qr[N];
static const double FS = 2000000.0;

static unsigned rng = 1u;
static double noise(void) {
    rng = rng * 1103515245u + 12345u;
    return ((double)((rng >> 16) & 0xffff) / 32768.0) - 1.0;
}

static void clear(void) {
    int n;
    for (n = 0; n < N; n++) { ir[n] = 0.0f; qr[n] = 0.0f; }
    rng = 1u;
}

static void add_tone(double hz, double amplitude) {
    int n;
    for (n = 0; n < N; n++) {
        double p = 2.0 * M_PI * hz * n / FS;
        ir[n] += (float)(amplitude * cos(p));
        qr[n] += (float)(amplitude * sin(p));
    }
}

static void add_noise(double amplitude) {
    int n;
    for (n = 0; n < N; n++) {
        ir[n] += (float)(amplitude * noise());
        qr[n] += (float)(amplitude * noise());
    }
}

static void test_a_pure_tone_is_all_line(void) {
    struct signal_carrier c;

    clear();
    add_tone(120000.0, 10.0);
    check_true("a tone is found",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_close("at the frequency it was put", c.offset_hz, 120000.0, 30.0);
    /* Nothing else in the samples, so the line is the whole of the power. */
    check_close("with all of the channel's energy in it", c.in_line, 1.0, 0.02);
    check_true("and it reads as a bare tone", signal_is_bare_tone(&c));
}

/*
 * Noise *inside the channel* is what dilutes the line -- and the channel is
 * the caller's choice.
 *
 * This originally added noise across the whole 2 MHz and expected the line to
 * be swamped. It is not: the channel filter throws almost all of that away,
 * so a tone under broadband noise still reads as a bare tone, which is
 * correct and was not what the test claimed. Only energy inside the width the
 * caller nominated counts.
 */
static void test_only_in_channel_energy_counts(void) {
    struct signal_carrier c;

    clear();
    add_tone(120000.0, 1.0);
    add_noise(4.0);        /* loud, but spread over 2 MHz */
    check_true("the tone is found under broadband noise",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_true("and a 40 kHz channel rejects most of it, so it reads bare",
               signal_is_bare_tone(&c));

    /* Enough that the channel itself is mostly noise. */
    clear();
    add_tone(120000.0, 1.0);
    add_noise(30.0);
    check_true("under noise loud enough to fill the channel",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_true("it no longer reads as a bare tone", !signal_is_bare_tone(&c));
}

/*
 * The failure this tool was written against.
 *
 * A receiver's DC offset sits at exactly +0 Hz and is the strongest thing in
 * any capture, at an empty frequency as readily as an occupied one. Three
 * analyses of 75.000 MHz were thrown away to it: the search found DC, measured
 * its sidebands, and reported confident numbers about nothing -- and the tell
 * was a control at an *empty* frequency reading a stronger carrier than the
 * signal under test.
 */
static void test_dc_is_not_a_carrier(void) {
    struct signal_carrier c;
    int n;

    clear();
    for (n = 0; n < N; n++) { ir[n] = 30.0f; qr[n] = -20.0f; }  /* pure DC */
    add_noise(1.0);
    check_true("a capture holding only a DC offset finds no carrier",
               signal_find_carrier(ir, qr, N, FS, -180000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_true("and what it finds is not at zero", fabs(c.offset_hz) >= 2000.0);
    check_true("nor does the offset masquerade as a bare tone",
               !signal_is_bare_tone(&c));

    /* With a real carrier present as well, the carrier wins -- which is what
       three attempts failed to do. */
    clear();
    for (n = 0; n < N; n++) { ir[n] = 30.0f; qr[n] = -20.0f; }
    add_tone(120000.0, 6.0);
    check_true("with DC and a carrier, one is found",
               signal_find_carrier(ir, qr, N, FS, -180000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_close("and it is the carrier, not the offset", c.offset_hz,
                120000.0, 30.0);
}

static void test_the_guard_is_the_callers(void) {
    struct signal_carrier c;
    int n;

    clear();
    for (n = 0; n < N; n++) { ir[n] = 30.0f; qr[n] = -20.0f; }
    add_noise(1.0);
    /* A caller that passes no guard means it, and gets DC. Documented rather
       than prevented: measuring at zero is a legitimate thing to want. */
    check_true("with no guard, DC is found",
               signal_find_carrier(ir, qr, N, FS, -180000.0, 180000.0, 0.0,
                                   40000.0, &c) == 1);
    check_close("at zero", c.offset_hz, 0.0, 60.0);
}

/*
 * An empty frequency is not a modulated one.
 *
 * The tool's first run over real captures called a control at an empty
 * frequency "modulated", because nothing there has a constant in it either:
 * in_line reads 0.00 for an empty channel exactly as it does for a busy one.
 * Only the height of the line separates them.
 */
static void test_empty_is_not_modulated(void) {
    struct signal_carrier c;

    clear();
    add_noise(3.0);
    check_true("something is returned for pure noise",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_str("but the verdict is that there is no carrier",
              signal_verdict_name(signal_carrier_verdict(&c)), "no carrier");

    clear();
    add_tone(120000.0, 10.0);
    check_true("a clean tone is found",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_str("and reads as bare",
              signal_verdict_name(signal_carrier_verdict(&c)),
              "a bare carrier");

    /* A carrier with something on it: swept across the channel, so the line
       is there and most of the channel is not it. */
    clear();
    {
        int n;
        for (n = 0; n < N; n++) {
            double t = (double)n / FS;
            double p = 2.0 * M_PI * (120000.0 + 8000.0 * sin(2.0*M_PI*300.0*t))
                       * t;
            ir[n] = (float)(8.0 * cos(p));
            qr[n] = (float)(8.0 * sin(p));
        }
    }
    check_true("a modulated carrier is found",
               signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                                   40000.0, &c) == 1);
    check_str("and reads as modulated",
              signal_verdict_name(signal_carrier_verdict(&c)),
              "a modulated carrier");
}

static void test_refusals(void) {
    struct signal_carrier c;

    check_int("a null result is refused",
              signal_find_carrier(ir, qr, N, FS, 0.0, 1000.0, 0.0, 1000.0,
                                  NULL), 0);
    check_int("too few samples is refused",
              signal_find_carrier(ir, qr, 16, FS, 0.0, 1000.0, 0.0, 1000.0,
                                  &c), 0);
    check_int("an empty window is refused",
              signal_find_carrier(ir, qr, N, FS, 1000.0, 1000.0, 0.0, 1000.0,
                                  &c), 0);
    check_int("and a refusal leaves nothing behind", c.found, 0);
}

int main(void) {
    test_a_pure_tone_is_all_line();
    test_only_in_channel_energy_counts();
    test_dc_is_not_a_carrier();
    test_the_guard_is_the_callers();
    test_empty_is_not_modulated();
    test_refusals();
    return check_report("where a carrier is, and whether anything rides it");
}
