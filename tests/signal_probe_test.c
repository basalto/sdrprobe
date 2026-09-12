/*
 * Where a carrier is, and whether anything is riding on it.
 */

#include <math.h>
#include <stdlib.h>

#include <stdio.h>

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
    check_close("with all of the channel's energy in it", c.carrier_power_fraction, 1.0, 0.02);
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
 * carrier_power_fraction reads 0.00 for an empty channel exactly as it does for a busy one.
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

/* ------------------------------------------------------------------ *
 * The symbol-rate line, for a signal that is not TETRA
 * ------------------------------------------------------------------ */

/*
 * A linearly modulated signal at `rate_bd`, built by holding a random symbol
 * for a symbol period and shaping it with a raised half-cosine. The shaping
 * is what puts the line there at all: a rectangular hold has no excess
 * bandwidth in the sense Oerder and Meyr need, and a perfectly constant
 * envelope has no line either. Nothing about this is TETRA -- the rate is a
 * third of it and the alphabet is different -- which is the point of the
 * check, since the measurement used to live inside the TETRA plugin.
 */
static void add_symbols(double rate_bd, double amplitude, double offset) {
    double sps = FS / rate_bd;
    int n;
    double sym_i = 0.0, sym_q = 0.0, prev_i = 0.0, prev_q = 0.0;
    long last = -1;

    for (n = 0; n < N; n++) {
        double pos = ((double)n - offset) / sps;
        long k = (long)(pos < 0.0 ? 0.0 : pos);
        double frac, w;
        if (k != last) {
            prev_i = sym_i; prev_q = sym_q;
            sym_i = noise() > 0.0 ? 1.0 : -1.0;
            sym_q = noise() > 0.0 ? 1.0 : -1.0;
            last = k;
        }
        frac = pos - (double)k;
        if (frac < 0.0) frac = 0.0;
        /* A raised half-cosine between the previous symbol and this one, so
           the envelope dips between them and rises on them. */
        w = 0.5 - 0.5 * cos(M_PI * frac);
        ir[n] += (float)(amplitude * (prev_i + (sym_i - prev_i) * w));
        qr[n] += (float)(amplitude * (prev_q + (sym_q - prev_q) * w));
    }
}

static void test_the_symbol_line_is_not_tetras(void) {
    double strength = 0.0, off = 0.0, on = 0.0;

    clear();
    add_symbols(50000.0, 0.5, 0.0);
    signal_symbol_line(ir, qr, N, FS, 50000.0, &on);
    signal_symbol_line(ir, qr, N, FS, 37000.0, &off);
    check_true("a line stands at the symbol rate", on > 0.05);
    check_true("and not at a rate nothing was sent at", off < on / 10.0);

    /*
     * The timing is the line's phase, so shifting the symbols shifts it. A
     * quarter of a symbol period in should read a quarter of a period out --
     * the check the phase sign is right, which a magnitude cannot make.
     */
    clear();
    add_symbols(50000.0, 0.5, 0.0);
    {
        double t0 = signal_symbol_line(ir, qr, N, FS, 50000.0, &strength);
        double t1, shifted;
        clear();
        add_symbols(50000.0, 0.5, 10.0);   /* 10 of 40 samples per symbol */
        t1 = signal_symbol_line(ir, qr, N, FS, 50000.0, &strength);
        shifted = t1 - t0;
        while (shifted < 0.0) shifted += 1.0;
        while (shifted >= 1.0) shifted -= 1.0;
        check_close("a quarter-symbol shift moves the timing a quarter",
                    shifted, 0.25, 0.05);
    }

    clear();
    add_noise(0.5);
    signal_symbol_line(ir, qr, N, FS, 50000.0, &strength);
    check_true("noise has no symbol line", strength < 0.02);

    clear();
    add_tone(30000.0, 0.5);
    signal_symbol_line(ir, qr, N, FS, 50000.0, &strength);
    check_true("nor has a bare carrier", strength < 0.02);

    check_close("a refusal returns nothing",
                signal_symbol_line(ir, qr, 8, FS, 50000.0, &strength), 0.0,
                1e-9);
    check_close("and says so through the strength", strength, 0.0, 1e-9);
    check_close("a rate past half the sample rate is refused",
                signal_symbol_line(ir, qr, N, FS, FS, &strength), 0.0, 1e-9);
}

/* ------------------------------------------------------------------ *
 * A repeating structure, at a period that is not a TETRA timeslot
 * ------------------------------------------------------------------ */

static void test_repeat_finds_a_grid_that_is_not_tetras(void) {
    static unsigned char stream[40000];
    struct signal_repeat r;
    const int period = 148;          /* a GSM burst, not TETRA's 255 */
    int n, fixed_positions = 40;

    /* A burst: the first `fixed_positions` of every period are the same every
       time, the rest are random. That mixture is what says a grid is there. */
    /*
     * The fixed part must not be periodic in itself. The first version of this
     * used `slot & 3`, which repeats every four symbols, so every lag that was
     * a multiple of four lined the training block up with a shifted copy of
     * itself: lag 300 scored 0.437 against the true period's 0.454 and the
     * search rightly refused to call that a finding. A real training sequence
     * is chosen not to do that.
     */
    static const unsigned char training[] = {
        3, 1, 0, 2, 2, 3, 1, 1, 0, 3, 2, 0, 1, 3, 3, 2,
        0, 1, 1, 3, 0, 2, 3, 0, 2, 2, 1, 0, 3, 1, 2, 3,
        1, 0, 0, 2, 3, 3, 1, 2
    };
    rng = 1u;
    for (n = 0; n < 40000; n++) {
        int slot = n % period;
        stream[n] = slot < fixed_positions
                        ? training[slot]
                        : (unsigned char)(((unsigned)(noise() * 1000.0)) & 3);
    }
    check_int("a 148-symbol grid is found",
              signal_repeat_find(stream, 40000, 100, 600, &r), 1);
    check_int("at its fundamental, not a multiple", r.period, period);
    check_true("the repeat stands over the runner-up",
               r.repeat > r.runner_up * 1.4f);
    check_int("and the profile is filled for it", r.profile_len, period);
    check_true("the fixed half of the burst reads fixed",
               r.fixed >= fixed_positions - 2);
    check_true("and the rest reads varying", r.varying > 50);

    rng = 1u;
    for (n = 0; n < 40000; n++)
        stream[n] = (unsigned char)(((unsigned)(noise() * 1000.0)) & 3);
    check_int("noise has no period",
              signal_repeat_find(stream, 40000, 100, 600, &r), 0);
    check_int("and a refusal leaves nothing behind", r.period, 0);

    check_int("too few symbols for the range is refused",
              signal_repeat_find(stream, 300, 100, 600, &r), 0);
    check_int("a null stream is refused",
              signal_repeat_find(NULL, 40000, 100, 600, &r), 0);
    check_int("an inverted range is refused",
              signal_repeat_find(stream, 40000, 600, 100, &r), 0);
}

/* ------------------------------------------------------------------ *
 * Folding at a period
 * ------------------------------------------------------------------ */

/*
 * A burst of `on` samples every `period`, and nothing between them. Folding
 * has to put the peak at the phase the burst sits at.
 */
static void add_bursts(size_t period, size_t on, size_t offset,
                       double amplitude) {
    int n;
    for (n = 0; n < N; n++) {
        size_t phase = ((size_t)n + period - offset % period) % period;
        if (phase < on) {
            double p = 2.0 * M_PI * 70000.0 * n / FS;
            ir[n] += (float)(amplitude * cos(p));
            qr[n] += (float)(amplitude * sin(p));
        }
    }
}

static void test_folding_finds_a_period_and_its_phase(void) {
    struct signal_fold f;

    clear();
    add_bursts(9600, 300, 2000, 1.0);
    add_noise(0.05);
    check_int("a burst every 9600 samples folds", 
              signal_fold_at(ir, qr, N, 9600, 128, 32, &f), 1);
    check_true("the peak stands over the floor", f.ratio > 3.0);
    /* The phase is where the burst starts, to a slot: 9600 over 300 slots is
       32 samples, and the correlation window is 128 more. */
    check_true("at the phase the burst was put at",
               f.phase + 200 >= 2000 && f.phase <= 2000 + 200);

    clear();
    add_noise(0.5);
    check_int("noise folds to nothing much",
              signal_fold_at(ir, qr, N, 9600, 128, 32, &f), 1);
    check_true("no phase stands out in noise", f.ratio < 2.0);

    /*
     * The slot must be where the sample falls *inside the period*, not
     * (position / step) modulo the slot count. The two agree only when the
     * step divides the lag, and when it does not the fold's own period is
     * slots*step rather than lag, so it drifts against the signal and smears
     * the peak. A step that does not divide the lag has to give the same
     * answer as one that does.
     */
    clear();
    add_bursts(9600, 300, 2000, 1.0);
    add_noise(0.05);
    {
        struct signal_fold divides, does_not;
        signal_fold_at(ir, qr, N, 9600, 128, 32, &divides);
        signal_fold_at(ir, qr, N, 9600, 100, 7, &does_not);
        check_true("a step that does not divide the lag still finds it",
                   does_not.ratio > divides.ratio / 2.0);
    }

    /* The slot count is capped, and raising the cap is what keeps this
       allocation-free. A long period gets fewer, coarser slots. */
    clear();
    add_bursts(9600, 300, 2000, 1.0);
    check_int("a long period still folds",
              signal_fold_at(ir, qr, N, 76800, 128, 32, &f), 1);
    check_true("with the slot count capped", f.slots <= SIGNAL_FOLD_SLOTS);

    check_int("a lag longer than the capture is refused",
              signal_fold_at(ir, qr, N, N, 128, 32, &f), 0);
    check_int("a zero step is refused",
              signal_fold_at(ir, qr, N, 9600, 128, 0, &f), 0);
    check_int("and a refusal leaves nothing behind", f.slots, 0);

    /*
     * The correlation is blind to a frequency offset, which is what makes it
     * usable before anything is tuned: an offset multiplies every term by the
     * same phase and the magnitude discards it.
     */
    clear();
    add_bursts(9600, 300, 2000, 1.0);
    {
        struct signal_fold plain, offset;
        int n;
        signal_fold_at(ir, qr, N, 9600, 128, 32, &plain);
        for (n = 0; n < N; n++) {
            double p = 2.0 * M_PI * 31000.0 * n / FS;
            double c = cos(p), s = sin(p);
            float re = ir[n], im = qr[n];
            ir[n] = (float)(re * c - im * s);
            qr[n] = (float)(re * s + im * c);
        }
        signal_fold_at(ir, qr, N, 9600, 128, 32, &offset);
        check_close("a 31 kHz offset does not move the fold", offset.ratio,
                    plain.ratio, plain.ratio * 0.05);
    }
}

/* ------------------------------------------------------------------ *
 * How long is a burst, and how much of the time is it there?
 * ------------------------------------------------------------------ */

/* A carrier keyed on for `on` samples every `period`, starting at `offset`
   so the first burst does not touch the edge of the buffer. */
static void add_keyed(size_t period, size_t on, size_t offset,
                      double amplitude) {
    int n;
    for (n = 0; n < N; n++) {
        size_t phase = ((size_t)n + period - offset % period) % period;
        if (phase < on) {
            double p = 2.0 * M_PI * 70000.0 * n / FS;
            ir[n] += (float)(amplitude * cos(p));
            qr[n] += (float)(amplitude * sin(p));
        }
    }
}

static void test_a_burst_is_measured_to_its_length(void) {
    struct signal_bursts b;
    const double gap = 0.0001;               /* SIGNAL_BURST_GAP_DEFAULT */

    /*
     * Ten bursts of 1 ms every 10 ms in 0.1 s. The lengths are exact because
     * the smoothing bias is subtracted: a causal average reaches a
     * floor-relative threshold almost at the true start and does not fall
     * back until the window has slid off the end, so every length is long by
     * exactly the window and every length has the window taken off it.
     */
    clear();
    add_keyed(20000, 2000, 1500, 1.0);
    add_noise(0.02);
    check_int("a keyed carrier has a burst structure",
              signal_find_bursts(ir, qr, N, FS, gap, &b), 1);
    check_int("and it is separable", b.verdict, SIGNAL_BURST_SEPARABLE);
    check_int("with one burst per period", b.count, 10);
    check_close("of the length it was keyed for", b.median_seconds,
                0.001, 0.00002);
    check_close("the shortest no shorter", b.shortest_seconds, 0.001,
                0.00002);
    check_close("the longest no longer", b.longest_seconds, 0.001, 0.00002);
    check_close("and the silence between them", b.median_gap_seconds,
                0.009, 0.0002);
    check_close("occupancy is the duty it was keyed at", b.occupancy,
                0.10, 0.012);

    /* The bias is the window and nothing else, so a different length reads
       correctly with no other change. */
    clear();
    add_keyed(20000, 600, 1500, 1.0);
    add_noise(0.02);
    signal_find_bursts(ir, qr, N, FS, gap, &b);
    check_close("a shorter burst reads shorter, by what it is",
                b.median_seconds, 0.0003, 0.00002);
    check_int("and there are still ten of them", b.count, 10);
}

static void test_what_is_not_a_burst_structure(void) {
    struct signal_bursts b;
    const double gap = 0.0001;

    /*
     * A bare carrier is a level, not a burst pattern. This is the case the
     * first version got wrong in the loudest possible way -- it reported
     * 134726 bursts in an unmodulated carrier, because a threshold in a *raw*
     * envelope finds the noise crossing it. The smoothing is what fixed it.
     */
    clear();
    add_tone(70000.0, 1.0);
    add_noise(0.05);
    check_int("a bare carrier has no burst structure",
              signal_find_bursts(ir, qr, N, FS, gap, &b), 0);
    check_int("and says which kind of nothing", b.verdict,
              SIGNAL_BURST_LEVEL);
    check_int("with no burst count to mislead anyone", b.count, 0);

    clear();
    add_noise(0.5);
    check_int("noise alone has none either",
              signal_find_bursts(ir, qr, N, FS, gap, &b), 0);
    check_int("and reads as a level, not as bursts", b.verdict,
              SIGNAL_BURST_LEVEL);

    /*
     * And a transmitter busy more of the time than not is reported busy
     * rather than as bursts. That is the conservative failure and it is
     * deliberate: an LTE downlink at the default gap reads 191 "bursts" of
     * 354 us, which are its own OFDM symbols, at 80% occupancy -- where Mode S
     * reads 0.4%. No threshold on contrast separates those two; occupancy
     * separates them by a factor of two hundred.
     */
    clear();
    add_keyed(20000, 12000, 1500, 1.0);      /* 60% duty */
    add_noise(0.02);
    check_int("a mostly-on transmitter is not reported as bursts",
              signal_find_bursts(ir, qr, N, FS, gap, &b), 0);
    check_int("it is reported busy", b.verdict, SIGNAL_BURST_BUSY);
    check_true("with the occupancy that says why",
               b.occupancy > SIGNAL_BURST_MAX_OCCUPANCY);
    check_int("and no median length it does not stand behind",
              b.median_seconds == 0.0, 1);
}

static void test_a_burst_cut_by_the_buffer_is_not_measured(void) {
    struct signal_bursts b;
    const double gap = 0.0001;

    /*
     * A run touching either end was cut by the buffer and not by the
     * transmitter, so its length is an artefact. Averaging half a burst into
     * the median biases every answer the same direction, which is the worst
     * kind of quiet error.
     */
    clear();
    add_keyed(20000, 2000, 0, 1.0);   /* the first burst starts at sample 0 */
    add_noise(0.02);
    signal_find_bursts(ir, qr, N, FS, gap, &b);
    check_true("the burst at the buffer's start is set aside",
               b.truncated >= 1);
    check_int("and not counted among the measured ones", b.count, 9);
    check_close("so the median is still the real length", b.median_seconds,
                0.001, 0.00002);
    /* It still counts towards occupancy, which asks how much of the buffer
       was busy and does not care where a burst began. */
    check_close("while occupancy keeps it", b.occupancy, 0.10, 0.012);
}

static void test_the_gap_says_what_is_one_burst(void) {
    struct signal_bursts wide, narrow;

    /*
     * Two 200-sample pulses 100 samples apart, every 20000. Whether that is
     * one burst or two is not a fact about the signal, it is what the caller
     * asked: a gap wider than the 50 us between them makes it one, a narrower
     * gap makes it two. Mode S is the case that needs this -- 0.5 us pulses
     * inside a frame -- and without it the answer is "hundreds of one-sample
     * bursts", which is true and useless.
     */
    clear();
    add_keyed(20000, 200, 1500, 1.0);
    add_keyed(20000, 200, 1800, 1.0);
    add_noise(0.02);
    signal_find_bursts(ir, qr, N, FS, 0.0002, &wide);
    signal_find_bursts(ir, qr, N, FS, 0.00002, &narrow);
    check_int("a wide gap makes the pair one burst", wide.count, 10);
    check_int("a narrow one makes it two", narrow.count, 20);
    check_true("and the one burst is the longer",
               wide.median_seconds > narrow.median_seconds * 1.5);
}

static void test_burst_refusals(void) {
    struct signal_bursts b;

    clear();
    add_keyed(20000, 2000, 1500, 1.0);
    check_int("a null destination is refused",
              signal_find_bursts(ir, qr, N, FS, 0.0001, NULL), 0);
    check_int("too few samples is refused",
              signal_find_bursts(ir, qr, 16, FS, 0.0001, &b), 0);
    check_int("and a refusal leaves nothing behind", b.count, 0);
    check_int("a negative gap is refused",
              signal_find_bursts(ir, qr, N, FS, -1.0, &b), 0);
    check_int("a sample rate of zero is refused",
              signal_find_bursts(ir, qr, N, 0.0, 0.0001, &b), 0);
}

/* ------------------------------------------------------------------ *
 * Does the envelope carry anything?
 * ------------------------------------------------------------------ */

static void test_the_envelope_of_a_bare_tone_does_not_vary(void) {
    struct signal_envelope e;

    clear();
    add_tone(70000.0, 0.5);
    check_int("a clean tone is measurable",
              signal_envelope_stats(ir, qr, N, FS, 70000.0, 40000.0, &e), 1);
    check_true("and its envelope does not vary", e.variation < 0.01);
    check_true("so its peak is its mean", e.peak_over_mean_db < 0.5);
    check_true("and its frequency does not move",
               e.frequency_spread_hz < 10.0);
    check_close("nor is it anywhere but where it was put",
                e.mean_frequency_hz, 0.0, 5.0);

    /* Mixing to the wrong frequency leaves the residual, which is what
       mean_frequency_hz is for: it is the offset the isolation did not
       remove, and a caller that trusted the carrier it was given can see how
       far out it was. */
    clear();
    add_tone(70000.0, 0.5);
    signal_envelope_stats(ir, qr, N, FS, 68000.0, 40000.0, &e);
    check_close("a carrier 2 kHz out reads 2 kHz of residual",
                e.mean_frequency_hz, 2000.0, 50.0);
}

static void test_noise_reads_rayleigh(void) {
    struct signal_envelope e;

    /*
     * A complex Gaussian's magnitude is Rayleigh, whose coefficient of
     * variation is sqrt(4/pi - 1) and depends on nothing. That is the
     * reference the whole statistic is read against, so it is worth pinning
     * against a number derived rather than measured -- and it is also what
     * OFDM reads, since a sum of many independent subcarriers is Gaussian by
     * the central limit theorem.
     */
    clear();
    add_noise(0.4);
    check_int("noise is measurable", 
              signal_envelope_stats(ir, qr, N, FS, 0.0, 500000.0, &e), 1);
    check_close("and reads Rayleigh", e.variation,
                SIGNAL_ENVELOPE_RAYLEIGH, 0.06);
    check_true("which is above what a contained envelope reads",
               SIGNAL_ENVELOPE_RAYLEIGH > SIGNAL_ENVELOPE_CONTAINED);
    check_true("and below what a restless one does",
               SIGNAL_ENVELOPE_RAYLEIGH < SIGNAL_ENVELOPE_RESTLESS);
}

static void test_an_on_off_envelope_varies_more_than_noise(void) {
    struct signal_envelope e;

    /* Keyed a fifth of the time: the envelope spends most of its time at
       nothing and the rest at full, which is more variation than noise has. */
    clear();
    add_keyed(20000, 4000, 1500, 1.0);
    add_noise(0.02);
    signal_envelope_stats(ir, qr, N, FS, 70000.0, 500000.0, &e);
    check_true("an on-off envelope varies more than noise",
               e.variation > SIGNAL_ENVELOPE_RESTLESS);
    check_true("and its peak stands far over its mean",
               e.peak_over_mean_db > 5.0);
}

static void test_envelope_refusals(void) {
    struct signal_envelope e;

    clear();
    add_tone(70000.0, 0.5);
    check_int("a null destination is refused",
              signal_envelope_stats(ir, qr, N, FS, 70000.0, 40000.0, NULL), 0);
    check_int("too few samples is refused",
              signal_envelope_stats(ir, qr, 64, FS, 70000.0, 40000.0, &e), 0);
    check_int("a channel as wide as the sample rate is refused",
              signal_envelope_stats(ir, qr, N, FS, 0.0, FS, &e), 0);
    check_int("and a refusal leaves nothing behind", e.found, 0);

    /*
     * Eight bits is about 45 dB, so a signal far down the range is measuring
     * the quantiser rather than the modulation. Refused rather than reported,
     * because a variation computed on three quantisation levels is a number
     * that looks exactly like a measurement.
     */
    clear();
    add_tone(70000.0, 0.0002);
    check_int("a signal in the quantiser's floor is refused",
              signal_envelope_stats(ir, qr, N, FS, 70000.0, 40000.0, &e), 0);
}

/* ------------------------------------------------------------------ *
 * On air
 * ------------------------------------------------------------------ */

/*
 * A real capture, because everything above is synthetic and a synthetic
 * signal agrees with whatever assumption built it. This repository has lost
 * months to that twice -- a conjugated primary sequence and a scattered SCH
 * field layout, both green throughout -- and the rule it learned is that a
 * real-signal invariant is the only check a shared mistake cannot satisfy.
 *
 * `carrier_75000_bare.bin` is 2 s of the 75.0005 MHz clock harmonic, recorded
 * 300 kHz below it so the carrier lands clear of the receiver's own DC
 * offset. It is the case the whole of `.scratch/signal-probe/` was raised
 * for: a signal the band plan calls an ILS marker beacon, standing 42 dB over
 * its floor, with nothing riding it.
 */
static void test_a_real_bare_carrier(void) {
    static float ci[400000], cq[400000];
    struct signal_carrier c;
    unsigned char raw[8];
    FILE *f = fopen("testfiles/carrier_75000_bare.bin", "rb");
    size_t n = 0;

    if (!f) {
        check_true("the capture is present", 0);
        return;
    }
    while (n < 400000 && fread(raw, 1, 2, f) == 2) {
        ci[n] = ((float)raw[0] - 127.5f) / 127.5f;
        cq[n] = ((float)raw[1] - 127.5f) / 127.5f;
        n++;
    }
    fclose(f);
    check_size("the capture reads", n, 400000);

    check_int("a carrier is found where it was put",
              signal_find_carrier(ci, cq, n, 2000000.0, 260000.0, 340000.0,
                                  150000.0, 20000.0, &c), 1);
    check_close("at +300 kHz, to the tuning error", c.offset_hz, 299478.0,
                200.0);
    check_true("standing well over the floor beside it",
               c.carrier_over_noise_db > 35.0);
    check_true("with most of the channel standing still",
               c.carrier_power_fraction > 0.80);
    check_int("so it is a bare carrier", signal_carrier_verdict(&c),
              SIGNAL_BARE);
    check_int("and signal_is_bare_tone agrees", signal_is_bare_tone(&c), 1);

    /*
     * And the window is the caller's, which this capture shows better than a
     * DC spike would: there is a real neighbour at +176 kHz -- 74.877 MHz, a
     * frequency the survey also raises as a candidate in this band -- at
     * 38 dB against the target's 47.
     *
     * **This assertion used to say the opposite, and it was pinning a bug.**
     * It read that a whole-span search "finds something else entirely" and "a
     * weaker one, so nothing flags the mistake" -- the search returned the
     * +176 kHz neighbour rather than the stronger carrier it was sitting
     * beside. That was the coarse grid's blind comb (see
     * `SIGNAL_COARSE_PAIRS`): the grid stepped four main lobes at a time, and
     * the target fell in a null of both probes bracketing it. A whole-span
     * search returns the **strongest** line in the span, which is what it
     * should always have done.
     *
     * The lesson the old assertion was reaching for survives, and is asserted
     * below on its own terms: a search told to look in the *wrong window*
     * answers confidently about whatever is in that window, and nothing in
     * the result says so. The DC offset is the other way this goes wrong and
     * is not what happens here: this receiver's offset is small enough that
     * it does not win, which is exactly why the guard has to be a parameter
     * rather than a threshold somebody tuned once.
     */
    {
        struct signal_carrier wide, elsewhere;
        check_int("a search across the whole span finds something",
                  signal_find_carrier(ci, cq, n, 2000000.0, -340000.0,
                                      340000.0, 0.0, 20000.0, &wide), 1);
        check_close("and it is the strongest line, which is the target",
                    wide.offset_hz, c.offset_hz, 200.0);
        check_true("at the same strength",
                   fabs(wide.carrier_over_noise_db -
                        c.carrier_over_noise_db) < 1.0);

        /* Windowed on the wrong place, though, it answers about the wrong
           signal with no less confidence -- which is the whole argument for
           the window being a parameter. */
        check_int("a window around the neighbour finds the neighbour",
                  signal_find_carrier(ci, cq, n, 2000000.0, 140000.0,
                                      210000.0, 0.0, 20000.0, &elsewhere), 1);
        check_true("which is a different signal",
                   fabs(elsewhere.offset_hz - c.offset_hz) > 100000.0);
        check_true("and a weaker one, with nothing in the result to say so",
                   elsewhere.carrier_over_noise_db <
                       c.carrier_over_noise_db);
    }

    /* A continuous carrier is a level, not a burst pattern. */
    {
        struct signal_bursts b;
        check_int("and it does not transmit in bursts",
                  signal_find_bursts(ci, cq, n, 2000000.0,
                                     SIGNAL_BURST_GAP_DEFAULT, &b), 0);
        check_int("it is a level", b.verdict, SIGNAL_BURST_LEVEL);
    }
}


/* ------------------------------------------------------------------ *
 * The standing fraction must not depend on how much signal it is handed
 * ------------------------------------------------------------------ */

/*
 * A carrier whose frequency creeps, which is what an uncalibrated crystal
 * does. Phase is the integral of frequency, so a constant drift enters as a
 * half-t-squared term.
 */
#define DRIFT_PAIRS 1000000
static float dri[DRIFT_PAIRS], drq[DRIFT_PAIRS];

static void drifting_tone(double hz, double drift_hz_per_s, double amplitude) {
    size_t n;
    for (n = 0; n < DRIFT_PAIRS; n++) {
        double t = (double)n / FS;
        double p = 2.0 * M_PI * (hz * t + 0.5 * drift_hz_per_s * t * t);
        dri[n] = (float)(amplitude * cos(p));
        drq[n] = (float)(amplitude * sin(p));
    }
}

static double standing_over(size_t pairs) {
    struct signal_carrier c;

    if (!signal_find_carrier(dri, drq, pairs, FS, 260000.0, 340000.0,
                             150000.0, 20000.0, &c))
        return -1.0;
    return c.carrier_power_fraction;
}

/*
 * The property `.scratch/standing-fraction-drifts/` was raised for, and the
 * whole point of segmenting: **the answer must not depend on the length of
 * the look.**
 *
 * `constant_fraction()` mixes at one fixed frequency and used to take the
 * mean over the entire buffer, so a carrier drifting even a fraction of a
 * hertz walked out of phase across a long observation and the mean cancelled
 * against itself -- which reads exactly like modulation, because modulation
 * is what the statistic is looking for. On the real 75.0005 MHz recording it
 * fell from 0.921 at 0.2 s to 0.779 at 2 s and flipped the verdict at
 * SIGNAL_BARE_FRACTION.
 *
 * Synthetic here, because a synthetic carrier separates the two candidate
 * causes that a capture cannot: length and drift. Measured against the
 * unsegmented form, this exact case read **0.9996 at 0.066 s and 0.0974 at
 * 0.5 s** -- so it is a check that failed before the fix and passes after,
 * which is the right way round.
 */
static void test_the_standing_fraction_does_not_depend_on_the_look(void) {
    double brief, long_look;

    drifting_tone(300000.0, 20.0, 1.0);
    brief = standing_over(131072);          /* one sample block, 0.066 s */
    long_look = standing_over(DRIFT_PAIRS); /* 0.5 s of the same carrier */

    check_true("the drifting carrier is found in a brief look", brief >= 0.0);
    check_true("and in a long one", long_look >= 0.0);
    check_close("a brief look sees a bare carrier", brief, 1.0, 0.02);
    check_close("and so does a look seven times longer", long_look, 1.0, 0.02);
    /* The property itself, rather than the two values separately: whatever
       the number is, it is the same number. */
    check_msg(fabs(long_look - brief) <= 0.02,
              "the standing fraction moved with the length of the look: "
              "%.4f over 0.066 s against %.4f over 0.500 s\n", brief,
              long_look);
}

/*
 * And with the drift taken out, the length changes nothing either -- which is
 * the control that says the fix addresses drift rather than length. The
 * unsegmented form passed this one too; it is here so that a future change
 * cannot trade one for the other.
 */
static void test_a_steady_carrier_reads_the_same_at_any_length(void) {
    double brief, long_look;

    drifting_tone(300000.0, 0.0, 1.0);
    brief = standing_over(131072);
    long_look = standing_over(DRIFT_PAIRS);
    check_close("a steady carrier is all line in a brief look", brief, 1.0,
                0.01);
    check_close("and in a long one", long_look, 1.0, 0.01);
}

/*
 * Segmenting must not have cost the dilution, which is the property the
 * statistic exists for: energy *inside the channel* beside the line is what
 * makes a carrier modulated rather than bare, and a wider channel admits
 * more of it.
 *
 * Written after two wrong guesses about what segmenting would do to noise,
 * both of which this suite already warns against. A tone under six times its
 * own amplitude in broadband noise still reads 0.679 in a 40 kHz channel --
 * the channel filter throws almost all of that noise away, exactly as
 * test_only_in_channel_energy_counts says. And noise with no tone in it reads
 * **0.00** through this entry point, not the segment's own 1/64 bias: the
 * search finds no coherent line to mix against, so the bias is not reachable
 * from outside and cannot be asserted here.
 *
 * What is assertable is the trend, and it is monotone: 0.679 at 40 kHz,
 * 0.514 at 80, 0.339 at 160, 0.209 at 320, 0.123 at 640.
 */
static void test_a_wider_channel_admits_more_of_the_noise(void) {
    struct signal_carrier c;
    double narrow, wide;

    clear();
    add_tone(120000.0, 1.0);
    add_noise(6.0);
    if (!signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                             40000.0, &c)) {
        check_true("a line is found under the noise", 0);
        return;
    }
    narrow = c.carrier_power_fraction;
    if (!signal_find_carrier(ir, qr, N, FS, 60000.0, 180000.0, 2000.0,
                             640000.0, &c)) {
        check_true("and in a wider channel", 0);
        return;
    }
    wide = c.carrier_power_fraction;
    check_close("a 40 kHz channel keeps most of the line", narrow, 0.679,
                0.05);
    check_close("sixteen times the channel admits sixteen times the noise",
                wide, 0.123, 0.05);
    check_msg(wide < narrow,
              "a wider channel did not dilute the line: %.3f at 640 kHz "
              "against %.3f at 40 kHz\n", wide, narrow);
}


/*
 * A line is found wherever it sits, and this is the check the old coarse grid
 * could not pass.
 *
 * The scan steps a grid and takes the best probe. A mix over C pairs has a
 * main lobe `rate/C` wide with **nulls at every multiple of it**, so if the
 * grid is wider than that lobe there are frequencies where both bracketing
 * probes land in a null and the line reads as nothing at either. The grid was
 * `rate/probe * 4`, four lobes, so the blind frequencies were a comb every
 * 40 Hz at this rate and look length.
 *
 * Measured on the shipped code before the fix, with **no noise at all**: a
 * tone at 120 000 Hz was found exactly; the same tone at 120 010 Hz and at
 * 120 030 Hz -- one and three lobes off a grid point -- was lost outright,
 * the search answering 36 kHz and 58 kHz away. Nothing caught it because
 * every fixture in this file uses 120 000 Hz, which falls exactly on the
 * grid, and the real captures happened to land elsewhere.
 *
 * So this walks a clean tone across a whole grid step. It is deliberately
 * noise-free: a null is a property of the grid, not of the signal-to-noise,
 * and mixing the two would let a future regression hide behind "it is weak".
 */
static void test_a_line_is_found_wherever_it_sits(void) {
    struct signal_carrier c;
    double step = FS / (double)N * 4.0;   /* the old grid, the blind one */
    int k;

    for (k = 0; k <= 8; k++) {
        double tone = 120000.0 + (double)k * step / 8.0;
        int n;

        clear();
        for (n = 0; n < N; n++) {
            double t = (double)n / FS;
            ir[n] = (float)(10.0 * cos(2.0 * M_PI * tone * t));
            qr[n] = (float)(10.0 * sin(2.0 * M_PI * tone * t));
        }
        /* A narrow window on purpose: a null is a local property of the
           grid, so 20 kHz exercises it exactly as 120 kHz does and costs a
           sixth as much. A lost line still lands far outside the 5 Hz this
           asserts -- verified against the pre-fix source, where three of
           these nine offsets fail. */
        if (!signal_find_carrier(ir, qr, N, FS, 110000.0, 130000.0, 2000.0,
                                 40000.0, &c)) {
            check_msg(0, "a clean tone at %.1f Hz was not found at all\n",
                      tone);
            continue;
        }
        check_msg(fabs(c.offset_hz - tone) < 5.0,
                  "a clean tone at %.1f Hz came back as %.1f Hz, %.0f Hz "
                  "out\n", tone, c.offset_hz, fabs(c.offset_hz - tone));
    }
}

/*
 * What the refinement actually converges to, at the one buffer length that
 * makes it tight.
 *
 * Nothing else here pins it. The synthetic carrier tests use 200000 pairs and
 * allow 30 Hz; the real capture reaches the 300000-pair probe cap and asserts
 * two searches against *each other* to 200 Hz. So the fine stage could stop
 * converging and lose an order of magnitude of resolution with the suite
 * green. A noise-free tone at the cap, walked across the grid, says it lands
 * within 2 Hz from six different positions.
 *
 * **Measured, both ways.** Truncating the cascade to a single stage fails
 * four of these six (reading 3.5 Hz off) and only two assertions elsewhere in
 * this file, neither about resolution.
 *
 * **And what it does not pin, which is worth knowing before trusting it.**
 * The refinement's first step is `rate / SIGNAL_COARSE_PAIRS / 4` = 7.63 Hz
 * at 2 MS/s while the probe's first null is `rate / probe` = 6.67 Hz, so the
 * worst a grid point can sit from the line is 3.8 Hz and the margin is 1.75.
 * That relation is **not** what this test protects: making the first step
 * four times coarser leaves every assertion here passing, because each stage
 * re-centres on the previous winner and the cascade recovers. Tried, not
 * assumed. If the probe cap is ever raised without touching
 * SIGNAL_COARSE_PAIRS or the quarter, this suite will not notice.
 */
#define REFINE_PAIRS 300000
static float rfi[REFINE_PAIRS], rfq[REFINE_PAIRS];

static void test_the_refinement_cannot_land_in_a_null(void) {
    const double rate = 2000000.0, base = 120000.0;
    /* Zero, half a refine step, a whole one, and half a coarse step: the
       positions where a grid that stepped too far would lose the line. */
    const double offsets[] = { 0.0, 1.9, 3.8, 7.6, 15.25, 22.9 };
    size_t k, n;

    for (k = 0; k < sizeof offsets / sizeof offsets[0]; k++) {
        double want = base + offsets[k];
        struct signal_carrier c;

        for (n = 0; n < REFINE_PAIRS; n++) {
            double a = 2.0 * M_PI * want * (double)n / rate;
            rfi[n] = (float)(100.0 * cos(a));
            rfq[n] = (float)(100.0 * sin(a));
        }
        check_int("a tone off the refinement grid is still found",
                  signal_find_carrier(rfi, rfq, REFINE_PAIRS, rate,
                                      base - 500.0, base + 500.0, 0.0,
                                      20000.0, &c), 1);
        check_msg(fabs(c.offset_hz - want) < 2.0,
                  "tone at %+.2f Hz off the grid read %.2f Hz (want %.2f)",
                  offsets[k], c.offset_hz, want);
    }
}

int main(void) {
    test_a_pure_tone_is_all_line();
    test_only_in_channel_energy_counts();
    test_dc_is_not_a_carrier();
    test_the_guard_is_the_callers();
    test_empty_is_not_modulated();
    test_a_line_is_found_wherever_it_sits();
    test_refusals();
    test_the_symbol_line_is_not_tetras();
    test_repeat_finds_a_grid_that_is_not_tetras();
    test_folding_finds_a_period_and_its_phase();
    test_a_burst_is_measured_to_its_length();
    test_what_is_not_a_burst_structure();
    test_a_burst_cut_by_the_buffer_is_not_measured();
    test_the_gap_says_what_is_one_burst();
    test_burst_refusals();
    test_the_envelope_of_a_bare_tone_does_not_vary();
    test_noise_reads_rayleigh();
    test_an_on_off_envelope_varies_more_than_noise();
    test_envelope_refusals();
    test_a_real_bare_carrier();
    test_the_standing_fraction_does_not_depend_on_the_look();
    test_a_steady_carrier_reads_the_same_at_any_length();
    test_a_wider_channel_admits_more_of_the_noise();
    test_the_refinement_cannot_land_in_a_null();
    return check_report("where a carrier is, and whether anything rides it");
}
