#include "check.h"

#include "fm_dsp.h"

#include <math.h>
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

int main(void) {
    test_the_discriminator();
    test_the_byte_path_matches();
    test_the_pilot_locks();
    test_the_pilot_measures_the_sample_clock();
    test_it_does_not_lock_to_nothing();
    test_the_phase_tracks();
    test_it_refuses_nonsense();

    return check_report("FM multiplex: discriminator and pilot");
}
