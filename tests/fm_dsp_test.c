#include "check.h"

#include "fm_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The FM front end: the discriminator, and the pilot the whole chain hangs
 * from.
 *
 * A word on what these checks are worth, because the register in
 * .claude/skills/dsp-validation says to say it. A synthesised multiplex
 * modulated onto a carrier and then discriminated is a round trip, and a round
 * trip agrees with itself whatever convention both halves share. What saves
 * most of this from that objection is that the quantity under test is not a
 * convention: 19000 Hz is a fact about a broadcast transmitter, and a loop
 * that reports 19000.0 on a real capture is being checked against the world.
 * That measurement is in check-pipelines against a capture; here it is
 * against a signal built to be 19 kHz, which catches the arithmetic and not a
 * shared misunderstanding.
 *
 * The conventions that a round trip genuinely cannot check -- biphase
 * polarity, the sense of the differential encoding -- are downstream of this
 * file and are flagged where they arrive.
 */

/*
 * 400 kHz rather than the capture's 2.048 MS/s.
 *
 * The synthetic multiplex needs to be long enough in *seconds*, not samples:
 * the reported frequency is averaged over 100 ms, so a buffer that holds
 * 128 ms of signal reads it a third of the way through its own convergence --
 * which is what "40 ppm measured as 30" turned out to be, an honest estimator
 * being asked too early. 400 kHz gives 0.65 s in the same array, six time
 * constants, and still clears FM_MIN_SAMPLE_RATE with room for the deviation
 * (75 kHz at 400 kHz is 1.2 radians a sample, nowhere near the wrap).
 *
 * The capture rate is exercised where it matters, against a real capture.
 */
#define RATE 400000.0
#define SAMPLES 262144

static float mpx[SAMPLES];
static float iq_i[SAMPLES];
static float iq_q[SAMPLES];
static float out[SAMPLES];

/* A deterministic noise source: a check that is only usually green is worse
   than no check, so nothing here uses rand(). */
static unsigned long seed = 0x2545F491UL;
static double noise(void) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)((seed >> 33) % 20001) / 10000.0 - 1.0;
}

/*
 * Build a broadcast multiplex and put it on a carrier.
 *
 * `pilot_hz` is the pilot as *this receiver counts it*, which is how a sample
 * clock error is expressed: a receiver 30 ppm fast counts a 19000 Hz pilot as
 * 19000.57 Hz, and nothing about the transmitter changed.
 */
static void build(double pilot_hz, double pilot_level, double audio_level,
                  double noise_level) {
    double deviation = 75000.0;      /* the broadcast maximum */
    double phase = 0.0;

    for (int n = 0; n < SAMPLES; n++) {
        double t = (double)n / RATE;
        double m = pilot_level * cos(2.0 * M_PI * pilot_hz * t);
        /* Something in the audio band, and something where the stereo
           difference signal lives, so the loop has to reject both. */
        m += audio_level * sin(2.0 * M_PI * 997.0 * t);
        m += audio_level * 0.5 * sin(2.0 * M_PI * 5000.0 * t);
        m += audio_level * 0.3 * cos(2.0 * M_PI * 38000.0 * t + 0.4);
        m += noise_level * noise();
        mpx[n] = (float)m;

        phase += 2.0 * M_PI * deviation * m / RATE;
        iq_i[n] = (float)cos(phase);
        iq_q[n] = (float)sin(phase);
    }
}

static void test_the_discriminator(void) {
    /* A carrier offset by a constant turns by a constant angle every sample:
       2*pi*f/rate. This is the discriminator's whole contract. */
    double offset = 100000.0;
    double expect = 2.0 * M_PI * offset / RATE;
    size_t n;

    for (int i = 0; i < 4096; i++) {
        double t = (double)i / RATE;
        iq_i[i] = (float)cos(2.0 * M_PI * offset * t);
        iq_q[i] = (float)sin(2.0 * M_PI * offset * t);
    }
    n = fm_discriminate_f(iq_i, iq_q, 4096, out, SAMPLES);
    check_size("one sample out per pair after the first", n, 4095);
    check_close("a constant offset turns a constant angle", out[100], expect,
                1e-6);
    check_close("and still does much later", out[4000], expect, 1e-6);

    /* A negative offset turns the other way. A discriminator that lost the
       sign would put the multiplex upside down and every symbol after it. */
    for (int i = 0; i < 4096; i++) {
        double t = (double)i / RATE;
        iq_i[i] = (float)cos(-2.0 * M_PI * offset * t);
        iq_q[i] = (float)sin(-2.0 * M_PI * offset * t);
    }
    fm_discriminate_f(iq_i, iq_q, 4096, out, SAMPLES);
    check_close("a negative offset turns the other way", out[100], -expect,
                1e-6);

    /* Zero offset is zero, not a wrap to +-pi. */
    for (int i = 0; i < 256; i++) { iq_i[i] = 1.0f; iq_q[i] = 0.0f; }
    fm_discriminate_f(iq_i, iq_q, 256, out, SAMPLES);
    check_close("an unmodulated carrier turns nowhere", out[10], 0.0, 1e-9);
}

static void test_the_byte_path_matches(void) {
    /*
     * The two entry points must agree. They are separate loops -- one takes
     * the receiver's bytes, one takes floats a check can build -- and a
     * difference between them would mean every check here was testing a path
     * no capture ever travels.
     */
    static uint8_t bytes[8192];
    static float from_bytes[4096];
    double offset = 60000.0;
    size_t a, b;

    for (int i = 0; i < 4096; i++) {
        double t = (double)i / RATE;
        double ci = cos(2.0 * M_PI * offset * t) * 100.0;
        double cq = sin(2.0 * M_PI * offset * t) * 100.0;
        bytes[2 * i] = (uint8_t)lround(ci + 127.5);
        bytes[2 * i + 1] = (uint8_t)lround(cq + 127.5);
        iq_i[i] = (float)((double)bytes[2 * i] - 127.5);
        iq_q[i] = (float)((double)bytes[2 * i + 1] - 127.5);
    }
    a = fm_discriminate(bytes, 4096, from_bytes, 4096);
    b = fm_discriminate_f(iq_i, iq_q, 4096, out, SAMPLES);
    check_size("both paths yield the same count", a, b);
    {
        double worst = 0.0;
        for (size_t i = 0; i < a; i++) {
            double d = fabs((double)from_bytes[i] - (double)out[i]);
            if (d > worst)
                worst = d;
        }
        check_close("and the same angles", worst, 0.0, 1e-6);
    }
}

static void test_the_pilot_locks(void) {
    struct fm_pilot pilot;
    size_t n;

    build(FM_PILOT_HZ, 0.10, 0.30, 0.0);
    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    check_true("the multiplex came back", n > 100000);

    fm_pilot_init(&pilot, RATE);
    check_true("nothing is locked before anything is fed",
               !fm_pilot_locked(&pilot));
    for (size_t i = 0; i < n; i++)
        fm_pilot_feed(&pilot, out[i]);

    check_true("a pilot under audio locks", fm_pilot_locked(&pilot));
    /* The number, not the lock: a loop that locks to the wrong tone reports a
       wrong frequency and everything downstream is built on it. */
    check_close("at 19 kHz", fm_pilot_hz(&pilot), FM_PILOT_HZ, 0.5);
    check_close("with nothing to say about this receiver",
                fm_pilot_ppm(&pilot), 0.0, 30.0);
}

/*
 * What the pilot says about the sample clock, and how well.
 *
 * A sweep rather than one offset, because one offset cannot tell an estimator
 * from a constant. The tolerance is measured rather than hoped for: across
 * -60 to +60 ppm and audio from a third to twice the pilot, the error runs
 * about -6 to +10 ppm, and it is the loud-audio cases that are worst -- the
 * audio pulls a loop that has to stay wide enough to track. Twelve is that
 * measurement with a little room, and it is deliberately not tighter: a
 * tolerance below what the thing can actually do is a check that fails on a
 * Tuesday for no reason anyone can find.
 *
 * Ten ppm is also why fm_pilot_ppm is reported and not fed to the calibration
 * gate, which wants a standard error under one (ADR-0004). The note in
 * fm_dsp.c gives the other, worse reason.
 */
static void test_the_pilot_measures_the_sample_clock(void) {
    static const double offsets[] = { -60.0, -20.0, 0.0, 20.0, 60.0 };
    static const double audio[] = { 0.10, 0.30, 0.60 };

    for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
    for (unsigned a = 0; a < sizeof(audio) / sizeof(audio[0]); a++) {
        struct fm_pilot pilot;
        double truth = FM_PILOT_HZ * (1.0 + offsets[o] * 1e-6);
        double got;
        size_t n;

        build(truth, 0.10, audio[a], 0.0);
        n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
        fm_pilot_init(&pilot, RATE);
        for (size_t i = 0; i < n; i++)
            fm_pilot_feed(&pilot, out[i]);

        check_msg(fm_pilot_locked(&pilot),
                  "%+.0f ppm under audio %.2f: no lock\n", offsets[o],
                  audio[a]);
        got = fm_pilot_ppm(&pilot);
        check_msg(fabs(got - offsets[o]) <= 12.0,
                  "%+.0f ppm under audio %.2f: measured %+.2f\n", offsets[o],
                  audio[a], got);
        /* And the frequency it came from, in hertz, since that is what the
           subcarrier is three times of. */
        check_msg(fabs(fm_pilot_hz(&pilot) - truth) <= 0.25,
                  "%+.0f ppm under audio %.2f: %.3f Hz, not %.3f\n",
                  offsets[o], audio[a], fm_pilot_hz(&pilot), truth);
    }
}

static void test_it_does_not_lock_to_nothing(void) {
    struct fm_pilot pilot;
    size_t n;

    /* Noise with no pilot in it at all. A lock here would mean every silent
       band reports a station. */
    build(FM_PILOT_HZ, 0.0, 0.30, 0.60);
    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    fm_pilot_init(&pilot, RATE);
    for (size_t i = 0; i < n; i++)
        fm_pilot_feed(&pilot, out[i]);
    check_true("no pilot, no lock", !fm_pilot_locked(&pilot));

    /* And a real pilot is not called locked before the loop has settled. */
    build(FM_PILOT_HZ, 0.10, 0.30, 0.0);
    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    fm_pilot_init(&pilot, RATE);
    for (size_t i = 0; i < 200 && i < n; i++)
        fm_pilot_feed(&pilot, out[i]);
    check_true("nor before the loop has settled", !fm_pilot_locked(&pilot));
}

/*
 * The phase, which is the reason the loop is coherent at all.
 *
 * The amplitude would be enough to say a pilot is there. What the subcarrier
 * needs is where it is, because 57 kHz is three times this phase -- so a loop
 * that reported the right frequency and a phase drifting against the input
 * would pass every check above and hand the RDS mixer a rotating carrier.
 */
static void test_the_phase_tracks(void) {
    struct fm_pilot pilot;
    size_t n;
    double worst = 0.0;

    build(FM_PILOT_HZ, 0.10, 0.20, 0.0);
    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    fm_pilot_init(&pilot, RATE);
    for (size_t i = 0; i < n; i++) {
        double used = fm_pilot_feed(&pilot, out[i]);
        /* Past the transient, the loop's phase must stay locked to the
           pilot's own. Compare against the phase the signal was built with,
           allowing the constant offset a PLL is entitled to. */
        if (i > n / 2) {
            double truth = 2.0 * M_PI * FM_PILOT_HZ * (double)(i + 1) / RATE;
            double d = fmod(used - truth, 2.0 * M_PI);
            static double reference = 1e9;
            if (reference > 1e8)
                reference = d;
            d -= reference;
            while (d > M_PI) d -= 2.0 * M_PI;
            while (d < -M_PI) d += 2.0 * M_PI;
            if (fabs(d) > worst)
                worst = fabs(d);
        }
    }
    check_true("the loop is locked at the end", fm_pilot_locked(&pilot));
    check_msg(worst < 0.15, "the phase wandered %.3f rad against the pilot\n",
              worst);
}

static void test_it_refuses_nonsense(void) {
    struct fm_pilot pilot;

    check_size("no input, no output", fm_discriminate(NULL, 100, out, SAMPLES),
               0);
    check_size("nor one lone pair",
               fm_discriminate_f(iq_i, iq_q, 1, out, SAMPLES), 0);
    check_size("and it stops at the room it was given",
               fm_discriminate_f(iq_i, iq_q, 4096, out, 10), 10);
    fm_pilot_init(&pilot, RATE);
    check_close("feeding no pilot returns a phase anyway",
                fm_pilot_feed(NULL, 0.0f), 0.0, 1e-12);
    check_true("and an uninitialised one is not locked",
               !fm_pilot_locked(NULL));
}


/*
 * The RDS front end, end to end on a synthesised multiplex.
 *
 * What this can and cannot show, stated plainly because the register in
 * .claude/skills/dsp-validation asks for it. It checks the machinery: that
 * the timing search finds the offset the symbols are actually on, that the
 * axis estimator finds the phase the channel left them at, that the
 * differential arithmetic is right, and that all of it survives audio and
 * noise. It cannot check whether "no change means zero" is the right way
 * round, because the encoder below and the decoder under test would both have
 * it backwards and agree. That one is settled by a real capture reading a
 * programme identification that matches its station, in the ticket after this.
 */

#define RDS_BITS 900
static unsigned char data_bits[RDS_BITS];

/* Build a multiplex carrying a known RDS bitstream, and put it on a carrier.
   `axis` rotates the subcarrier away from the pilot's third harmonic, which
   a real channel does and the estimator has to undo. */
static void build_rds(double audio_level, double noise_level, double axis) {
    double phase = 0.0;
    unsigned char encoded[RDS_BITS + 1];

    /* Differential encoding: the channel bit changes when the data bit is 1,
       which is what makes the product of consecutive symbols the data. */
    encoded[0] = 0;
    for (int k = 0; k < RDS_BITS; k++) {
        data_bits[k] = (unsigned char)(noise() > 0.0 ? 1 : 0);
        encoded[k + 1] = (unsigned char)(encoded[k] ^ data_bits[k]);
    }

    for (int n = 0; n < SAMPLES; n++) {
        double t = (double)n / RATE;
        double m = 0.10 * cos(2.0 * M_PI * FM_PILOT_HZ * t);
        double symbol_pos = t * FM_RDS_SYMBOL_RATE_HZ;
        int index = (int)symbol_pos;
        double frac = symbol_pos - index;
        double level, biphase;

        m += audio_level * sin(2.0 * M_PI * 997.0 * t);
        m += audio_level * 0.5 * sin(2.0 * M_PI * 5000.0 * t);
        m += audio_level * 0.3 * cos(2.0 * M_PI * 38000.0 * t + 0.4);

        /* Biphase: half a symbol at the level, half at its negative. */
        level = encoded[(index + 1) % (RDS_BITS + 1)] ? -1.0 : 1.0;
        biphase = frac < 0.5 ? level : -level;
        /* Suppressed carrier: the data multiplies the subcarrier, which is
           exactly three times the pilot, turned by `axis`. */
        m += 0.03 * biphase * cos(2.0 * M_PI * FM_RDS_SUBCARRIER_HZ * t + axis);

        m += noise_level * noise();
        mpx[n] = (float)m;
        phase += 2.0 * M_PI * 75000.0 * m / RATE;
        iq_i[n] = (float)cos(phase);
        iq_q[n] = (float)sin(phase);
    }
}

/* Run a built signal all the way to soft bits. Returns how many, and where
   the decoder thinks the timing and the axis are. */
static size_t run_chain(float *soft, size_t capacity, int *offset,
                        double *axis) {
    static float bb_i[32768], bb_q[32768];
    struct fm_rds_front front;
    size_t n, bb;

    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    if (fm_rds_front_init(&front, RATE) < 0)
        return 0;
    bb = fm_rds_front_feed(&front, out, n, bb_i, bb_q, 32768);
    return fm_rds_soft_bits(bb_i, bb_q, bb, soft, capacity, offset, axis);
}

/*
 * How many of the decoded bits match what was sent, once the two streams are
 * aligned. The chain drops whatever arrives before the pilot locks and one
 * symbol to the differential, so the offset into the sent stream is not
 * known in advance and is found by sliding.
 */
static double best_agreement(const float *soft, size_t count) {
    double best = 0.0;

    if (count < 100)
        return 0.0;
    for (int shift = 0; shift + (int)count <= RDS_BITS; shift++) {
        size_t agree = 0;
        for (size_t k = 0; k < count; k++) {
            int decoded = soft[k] < 0.0f ? 1 : 0;
            if (decoded == data_bits[shift + (int)k])
                agree++;
        }
        if ((double)agree / count > best)
            best = (double)agree / count;
    }
    return best;
}

static float soft_bits[4096];

static void test_the_subcarrier_comes_down(void) {
    struct fm_rds_front front;
    size_t n, bb;
    static float bb_i[32768], bb_q[32768];

    build_rds(0.30, 0.0, 0.0);
    n = fm_discriminate_f(iq_i, iq_q, SAMPLES, out, SAMPLES);
    check_int("the front end takes the capture rate",
              fm_rds_front_init(&front, RATE), 0);
    bb = fm_rds_front_feed(&front, out, n, bb_i, bb_q, 32768);

    /* One sample per pilot cycle, from wherever the pilot locked. The signal
       is 0.655 s and the lock gate opens at 0.25, so what is left is about
       0.4 s of 19 kHz. */
    check_true("baseband came out", bb > 5000);
    check_true("at about the pilot rate, not the capture rate",
               bb < (size_t)(0.7 * FM_PILOT_HZ));
    check_true("and nothing came out before the pilot locked",
               bb < (size_t)(0.42 * FM_PILOT_HZ));

    /* A rate that cannot hold the subcarrier is refused rather than decoded
       into nonsense. */
    check_true("a rate too low for the multiplex is refused",
               fm_rds_front_init(&front, 100000.0) < 0);
}

static void test_the_bits_come_back(void) {
    int offset = -1;
    double axis = 99.0;
    size_t count;

    build_rds(0.30, 0.0, 0.0);
    count = run_chain(soft_bits, 4096, &offset, &axis);
    check_true("soft bits came out", count > 300);
    check_true("the timing landed inside a symbol",
               offset >= 0 && offset < FM_RDS_SAMPLES_PER_SYMBOL);
    {
        double agreement = best_agreement(soft_bits, count);
        check_msg(agreement > 0.99,
                  "a clean multiplex decoded %.1f%% of its bits\n",
                  agreement * 100.0);
    }
}

/*
 * The axis, which is the part a suppressed carrier makes necessary.
 *
 * A channel turns the subcarrier by whatever it likes and nothing transmits
 * the angle, so the decoder works it out by squaring. Every angle has to come
 * back -- including the two that would work by accident if the estimator did
 * nothing at all.
 */
static void test_any_axis_works(void) {
    static const double angles[] = { 0.0, 0.4, 1.0, M_PI / 2.0, 2.2, 3.0 };

    for (unsigned a = 0; a < sizeof(angles) / sizeof(angles[0]); a++) {
        int offset;
        double axis;
        size_t count;
        double agreement;

        build_rds(0.30, 0.0, angles[a]);
        count = run_chain(soft_bits, 4096, &offset, &axis);
        agreement = best_agreement(soft_bits, count);
        check_msg(agreement > 0.99,
                  "at axis %.2f rad, %.1f%% of bits decoded\n", angles[a],
                  agreement * 100.0);
    }
}

static void test_it_survives_noise(void) {
    int offset;
    double axis;
    size_t count;
    double agreement;

    build_rds(0.30, 0.05, 0.7);
    count = run_chain(soft_bits, 4096, &offset, &axis);
    agreement = best_agreement(soft_bits, count);
    check_msg(agreement > 0.95, "under noise, %.1f%% of bits decoded\n",
              agreement * 100.0);

    /* And with nothing on the subcarrier at all, the answer must not look
       like a decode: a chain that reports confident bits from an empty band
       is worse than one that reports none. */
    build(FM_PILOT_HZ, 0.10, 0.30, 0.05);
    count = run_chain(soft_bits, 4096, &offset, &axis);
    if (count > 100) {
        double none = best_agreement(soft_bits, count);
        check_msg(none < 0.75,
                  "an empty subcarrier agreed with the data %.1f%% of the "
                  "time\n", none * 100.0);
    }
}

static void test_soft_bits_refuse_nonsense(void) {
    int offset;
    double axis;

    check_size("no baseband, no bits",
               fm_rds_soft_bits(NULL, NULL, 1000, soft_bits, 4096, &offset,
                                &axis), 0);
    check_size("nor too little of it",
               fm_rds_soft_bits(soft_bits, soft_bits, 8, soft_bits, 4096,
                                &offset, &axis), 0);
    check_size("nor no front end at all",
               fm_rds_front_feed(NULL, out, 100, soft_bits, soft_bits, 4096),
               0);
}


/*
 * The real capture, which is the only check here that is worth anything about
 * the *conventions*.
 *
 * Everything above is a round trip: an encoder I wrote feeding a decoder I
 * wrote, and the two would agree just as happily if both had the differential
 * sense backwards. What settles it is the published block code -- a (26,16)
 * shortened cyclic code with five offset words, IEC 62106 -- run over bits
 * that came off the air. The syndrome routine below is deliberately a second
 * implementation living in the check rather than a call into the source: an
 * oracle that shares code with the thing it is checking is not an oracle.
 *
 * If the chain is right, syndromes land far above chance AND they land on one
 * 26-bit alignment, which is a much harder thing to do by accident than
 * simply being above chance.
 *
 * Measured on this capture: 94 hits, 78 of them on residue 11, out of the 91
 * blocks two seconds can hold -- so 86% of blocks decode cleanly, and chance
 * across all 26 residues together would give about 11.
 */

#define RDS_GENERATOR 0x5B9u   /* x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1 */

static unsigned rds_syndrome(unsigned long block) {
    unsigned long reg = 0;
    int i;

    for (i = 25; i >= 0; i--) {
        reg = (reg << 1) | ((block >> i) & 1ul);
        if (reg & 0x400ul)
            reg ^= (0x400ul | RDS_GENERATOR);
    }
    return (unsigned)(reg & 0x3FFul);
}

static int rds_is_offset(unsigned syndrome_value) {
    static const unsigned words[] = { 0x0FC, 0x198, 0x168, 0x350, 0x1B4 };
    unsigned i;

    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (words[i] == syndrome_value)
            return 1;
    return 0;
}

/* Hits at every 26-bit alignment, for one polarity of the soft bits. */
static long rds_scan(const float *soft, size_t count, int invert,
                     long residues[26]) {
    long total = 0;
    size_t i;
    int r;

    for (r = 0; r < 26; r++)
        residues[r] = 0;
    for (i = 0; i + 26 <= count; i++) {
        unsigned long block = 0;
        int k;
        for (k = 0; k < 26; k++) {
            int bit = (soft[i + (size_t)k] < 0.0f) ^ invert;
            block = (block << 1) | (unsigned long)bit;
        }
        if (rds_is_offset(rds_syndrome(block))) {
            residues[i % 26]++;
            total++;
        }
    }
    return total;
}

static void test_a_real_capture_decodes(void) {
    const char *path = "testfiles/fm_rds_89600.bin";
    FILE *f = fopen(path, "rb");
    static uint8_t raw[8192000];
    /*
     * Its own multiplex buffer, and it has to hold the whole capture.
     *
     * The synthetic tests share `out`, which is 262144 samples -- 0.65 s at
     * the rate they run and 0.128 s at the capture's 2.048 MS/s, which is
     * less than the quarter second the pilot needs before it may declare a
     * lock. Truncating here produced no bits at all and looked like a broken
     * chain rather than a small buffer.
     */
    static float capture_mpx[4096000];
    static float bb_i[64000], bb_q[64000];
    static float soft[8192];
    struct fm_rds_front front;
    size_t bytes, n, bb, bits;
    long residues[26], total, best = 0, spread = 0;
    int offset, r, at = 0;
    double axis;

    if (!f) {
        check_msg(0, "cannot open %s -- run from the repository root\n", path);
        return;
    }
    bytes = fread(raw, 1, sizeof(raw), f);
    fclose(f);
    check_true("the capture is the size its sidecar says", bytes == 8192000);

    n = fm_discriminate(raw, bytes / 2, capture_mpx,
                        sizeof(capture_mpx) / sizeof(capture_mpx[0]));
    check_true("the whole two seconds came back", n > 4000000);

    check_int("the front end takes 2.048 MS/s",
              fm_rds_front_init(&front, 2048000.0), 0);
    bb = fm_rds_front_feed(&front, capture_mpx, n, bb_i, bb_q, 64000);

    /* The pilot, against a fact rather than against my own encoder: a
       broadcast pilot is 19 kHz and this receiver's own error is what the
       difference measures. An independent spectrum of the same capture puts
       it at 19000.00 Hz. */
    check_true("the pilot locks on a real station", fm_pilot_locked(&front.pilot));
    check_close("at 19 kHz", fm_pilot_hz(&front.pilot), FM_PILOT_HZ, 1.0);
    check_true("and baseband came out of it", bb > 20000);

    bits = fm_rds_soft_bits(bb_i, bb_q, bb, soft, 8192, &offset, &axis);
    check_true("so did soft bits", bits > 1500);
    /* The axis is whatever the channel left it at and is not predictable;
       what matters is that it is not the zero a broken estimator returns. */
    check_true("on an axis the estimator actually found", fabs(axis) > 0.01);

    total = rds_scan(soft, bits, 0, residues);
    for (r = 0; r < 26; r++) {
        if (residues[r] > best) { best = residues[r]; at = r; }
        spread += residues[r];
    }
    /*
     * 78 measured of the 91 a two-second capture can hold. Sixty leaves room
     * for a worse day without letting a broken chain through, and is still
     * seven times what chance gives across every residue at once.
     */
    check_msg(best >= 60,
              "only %ld blocks passed their syndrome on one alignment "
              "(residue %d of 26, %ld hits in total)\n", best, at, spread);
    check_msg((double)best / (double)spread >= 0.6,
              "the hits did not concentrate on one alignment: %ld of %ld "
              "(%.0f%%)\n", best, spread, 100.0 * best / spread);

    /*
     * And the polarity, which is the convention no round trip can settle.
     * Reading the differential the other way round -- "no change means one" --
     * must give chance and nothing more.
     */
    {
        long wrong = rds_scan(soft, bits, 1, residues);
        long wrong_best = 0;
        for (r = 0; r < 26; r++)
            if (residues[r] > wrong_best) wrong_best = residues[r];
        check_msg(wrong < total / 3,
                  "reading the differential backwards gave %ld hits against "
                  "%ld the right way round; the sense is not being tested\n",
                  wrong, total);
        check_msg(wrong_best < best / 3,
                  "and it found an alignment too: %ld against %ld\n",
                  wrong_best, best);
    }
}

int main(void) {
    test_the_discriminator();
    test_the_byte_path_matches();
    test_the_pilot_locks();
    test_the_pilot_measures_the_sample_clock();
    test_it_does_not_lock_to_nothing();
    test_the_phase_tracks();
    test_it_refuses_nonsense();
    test_the_subcarrier_comes_down();
    test_the_bits_come_back();
    test_any_axis_works();
    test_it_survives_noise();
    test_soft_bits_refuse_nonsense();
    test_a_real_capture_decodes();

    return check_report("FM multiplex: pilot, subcarrier and soft bits");
}
