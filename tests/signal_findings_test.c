/*
 * What the candidate panel says, and what it refuses to say.
 *
 * The findings are a pure function of the measurements, so all of this is
 * reachable with no window and no receiver (ADR-0012). What is *not* reachable
 * here is whether the panel reads well, which needs a screenshot.
 */

#include <string.h>

#include "check.h"
#include "signal_findings.h"

static int mentions(const struct signal_findings *f, const char *needle) {
    int k;
    for (k = 0; k < f->count; k++)
        if (strstr(f->line[k], needle))
            return 1;
    return 0;
}

/* The 75.000 MHz clock harmonic: a carrier 49.8 dB over its floor with 87% of
   the channel standing still. */
static struct signal_carrier bare(void) {
    struct signal_carrier c;
    memset(&c, 0, sizeof(c));
    c.found = 1;
    c.carrier_over_noise_db = 49.8;
    c.carrier_power_fraction = 0.872;
    return c;
}

/* fm_rds_tsf.bin: a real line, and nothing standing still behind it. */
static struct signal_carrier modulated(void) {
    struct signal_carrier c;
    memset(&c, 0, sizeof(c));
    c.found = 1;
    c.carrier_over_noise_db = 18.2;
    c.carrier_power_fraction = 0.001;
    return c;
}

/* adsb_cpr_pair.bin: real energy, no standing carrier at all. */
static struct signal_carrier pulsed(void) {
    struct signal_carrier c;
    memset(&c, 0, sizeof(c));
    c.found = 1;
    c.carrier_over_noise_db = -2.0;
    c.carrier_power_fraction = 0.0;
    return c;
}

static void test_a_bare_carrier_says_there_is_nothing_to_decode(void) {
    struct signal_carrier c = bare();
    struct signal_findings f;

    check_true("a bare carrier is reported",
               signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 100.0, &f) >= 2);
    check_true("named as one", mentions(&f, "a bare carrier"));
    check_true("with the fraction that says so", mentions(&f, "87%"));
    check_true("and the height it stands at", mentions(&f, "50 dB"));
    check_true("and the conclusion a reader wants",
               mentions(&f, "nothing here to decode"));
    /*
     * The refusal about the symbol rate belongs to a modulated carrier and
     * only to one. On a bare tone there is nothing to have a symbol rate, so
     * saying nobody looked would be noise.
     */
    check_int("and no symbol-rate refusal, which would be noise here",
              mentions(&f, "symbol rate"), 0);
}

static void test_a_modulated_carrier_says_what_was_not_asked(void) {
    struct signal_carrier c = modulated();
    struct signal_findings f;

    signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 100.0, &f);
    check_true("a modulated carrier is named",
               mentions(&f, "a modulated carrier"));
    check_true("and a fraction under half a percent is words, not \"0%\"",
               mentions(&f, "almost none"));
    check_int("because a rounded zero reads as a missing number",
              mentions(&f, "0%"), 0);
    {
        /* Above the rounding it is a number again. */
        struct signal_carrier big = modulated();
        struct signal_findings g;
        big.carrier_power_fraction = 0.12;
        signal_findings_from(&big, NULL, NULL, 1.0, 31, 31, 100.0, &g);
        check_true("a fraction that survives rounding is printed",
                   mentions(&g, "12%"));
    }
    check_true("and it does not guess what",
               mentions(&f, "what, this cannot say"));
    check_true("and it says the symbol rate was not looked for",
               mentions(&f, "no symbol rate looked for"));
    check_true("and why", mentions(&f, "needs one channel"));
    /* The claim is a measurement, never an identification. ADR-0015's rule
       for the band plan, applied to the signal. */
    check_int("no technology is named", mentions(&f, "TETRA"), 0);
}

static void test_a_pulse_is_not_a_bare_tone(void) {
    struct signal_carrier c = pulsed();
    struct signal_findings f;

    signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 100.0, &f);
    check_true("no standing carrier is the answer",
               mentions(&f, "no standing carrier"));
    check_int("not a bare one", mentions(&f, "a bare carrier"), 0);
    /*
     * Both halves of the caveat, and they are different failures. Noise
     * reaches 8-14 dB over its own median because a search takes the largest
     * of thousands of samples; a pulsed transmission has real energy and no
     * standing carrier. A reader told only the first looks for a fault in the
     * receiver, and told only the second believes a signal is there.
     */
    check_true("with the floor a search on noise alone reaches",
               mentions(&f, "noise alone reaches"));
    check_true("and that a pulse reads the same way",
               mentions(&f, "pulsed transmission"));
    /*
     * That hedge is only printed while nothing settles it. Given a burst
     * measurement that does, the panel says which of the two it is instead of
     * naming both -- a measurement replacing a disjunction.
     */
    {
        struct signal_bursts pulses;
        struct signal_findings g;
        memset(&pulses, 0, sizeof(pulses));
        pulses.verdict = SIGNAL_BURST_SEPARABLE;
        pulses.count = 4;
        pulses.median_seconds = 0.000120;
        pulses.median_gap_seconds = 0.012;
        pulses.occupancy = 0.0037;
        signal_findings_from(&c, &pulses, NULL, 1.0, 31, 31, 100.0, &g);
        check_int("with the envelope measured, the hedge goes",
                  mentions(&g, "pulsed transmission"), 0);
        check_true("and the bursts are named", mentions(&g, "in bursts"));
        check_true("with the length", mentions(&g, "120 us"));
        check_true("and how often", mentions(&g, "12.0 ms"));
        check_true("and how much of the time", mentions(&g, "0.37%"));
    }
    {
        /* A transmitter that never stops is its own finding. */
        struct signal_bursts busy;
        struct signal_findings g;
        memset(&busy, 0, sizeof(busy));
        busy.verdict = SIGNAL_BURST_BUSY;
        busy.occupancy = 0.80;
        signal_findings_from(&c, &busy, NULL, 1.0, 31, 31, 100.0, &g);
        check_true("a busy envelope is reported as busy",
                   mentions(&g, "does not stop"));
        check_true("with the occupancy behind it", mentions(&g, "80%"));
        check_int("and no burst length it does not have",
                  mentions(&g, "in bursts"), 0);
    }
}

static void test_the_refusals_are_kept(void) {
    struct signal_findings f;

    check_int("nothing measured yet says so rather than staying silent",
              signal_findings_from(NULL, NULL, NULL, 0.0, 0, 0, 0.0, &f), 1);
    check_true("and says what it would need",
               mentions(&f, "live receiver"));

    {
        struct signal_carrier c = bare();
        c.found = 0;
        signal_findings_from(&c, NULL, NULL, 0.0, 0, 0, 0.0, &f);
        check_true("a refusal from the measurement reads the same",
                   mentions(&f, "not looked at yet"));
    }
}

static void test_duty_and_drift_qualify_the_claim(void) {
    struct signal_carrier c = bare();
    struct signal_findings f;

    /* The bursty-signals case: three looks in thirty-one. */
    signal_findings_from(&c, NULL, NULL, 3.0 / 31.0, 3, 31, 100.0, &f);
    check_true("a low duty is reported", mentions(&f, "comes and goes"));
    check_true("with the count behind it", mentions(&f, "3 of 31"));
    check_true("and the count is what makes it a finding",
               mentions(&f, "3 of 31"));

    /* Steady in time, wandering in frequency. */
    signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 6400.0, &f);
    check_int("a steady signal is not called bursty",
              mentions(&f, "comes and goes"), 0);
    check_true("but its drift is", mentions(&f, "in frequency"));
    check_true("with the spread", mentions(&f, "6.4 kHz"));

    /* Steady in both: neither line, and no invented reassurance. */
    signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 100.0, &f);
    check_int("a steady signal draws neither qualifier",
              mentions(&f, "comes and goes") || mentions(&f, "in frequency"),
              0);

    /* Never measured: the duty lines need a denominator. */
    signal_findings_from(&c, NULL, NULL, 0.0, 0, 0, 0.0, &f);
    check_int("an unmeasured duty is not reported as zero",
              mentions(&f, "comes and goes"), 0);
}

static void test_it_never_overruns(void) {
    struct signal_carrier c = modulated();
    struct signal_findings f;
    int k;

    /* Every qualifier at once: modulated, bursty, and drifting. */
    signal_findings_from(&c, NULL, NULL, 0.05, 1, 31, 90000.0, &f);
    check_true("the line count stays inside the array",
               f.count <= SIGNAL_FINDING_LINES);
    /*
     * And every line fits the panel. sdrgui_text_fit() truncates rather than
     * wraps, and the first draft of these lost "...only 1% of the ch" and
     * "...that needs the chann" -- in both cases the half that carried the
     * qualification. A caveat cut off before its qualifier is worse than none.
     */
    for (k = 0; k < f.count; k++)
        check_msg(strlen(f.line[k]) <= SIGNAL_FINDING_FIT,
                  "line %d fits the panel: %zu of %d chars -- \"%s\"",
                  k, strlen(f.line[k]), SIGNAL_FINDING_FIT, f.line[k]);
    for (k = 0; k < f.count; k++)
        check_true("and every line is terminated",
                   strlen(f.line[k]) < SIGNAL_FINDING_TEXT);
    check_int("a null destination is refused",
              signal_findings_from(&c, NULL, NULL, 1.0, 31, 31, 0.0, NULL), 0);
}

static void test_the_envelope_is_named_only_where_it_separates(void) {
    struct signal_carrier c = modulated();
    struct signal_envelope e;
    struct signal_findings f;

    memset(&e, 0, sizeof(e));
    e.found = 1;

    e.variation = 0.032;                 /* FM broadcast, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_true("a contained envelope is named",
               mentions(&f, "hardly varies"));
    check_true("with the number", mentions(&f, "0.03"));
    check_true("and what noise would read", mentions(&f, "0.52"));

    e.variation = 1.098;                 /* an LTE downlink, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_true("a restless one is named too",
               mentions(&f, "varies past noise"));

    /*
     * TETRA's 0.252 is under the threshold, so it is named contained -- which
     * is true of it. What the sentence does not do is separate it from a bare
     * carrier in noise, which reads 0.248: the same number, named the same
     * way. That is the statistic's limit and not a fault in the sentence.
     */
    e.variation = 0.252;                 /* TETRA, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_true("filtered phase modulation reads contained",
               mentions(&f, "hardly varies"));
    e.variation = 0.248;                 /* a bare carrier in noise, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_true("and so does a bare carrier in noise, identically",
               mentions(&f, "hardly varies"));

    /* The band between the thresholds is where an empty channel sits, so
       nothing is said about it: a sentence there would be about nothing. */
    e.variation = 0.545;                 /* an empty channel, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_int("noise itself is left unnamed",
              mentions(&f, "envelope"), 0);
    e.variation = 0.792;                 /* GSM at one carrier, measured */
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_int("and so is everything else in that band",
              mentions(&f, "envelope"), 0);

    /* A refusal says nothing rather than reading as zero variation, which
       would be the strongest claim the statistic can make. */
    memset(&e, 0, sizeof(e));
    signal_findings_from(&c, NULL, &e, 1.0, 31, 31, 100.0, &f);
    check_int("an unmeasured envelope makes no claim",
              mentions(&f, "envelope"), 0);
}

/*
 * Every branch, against the panel's width. The one above covers the longest
 * case; this covers the rest, because a sentence is truncated by how wide it
 * is and not by how many qualifiers preceded it.
 */
static void test_every_branch_fits_the_panel(void) {
    struct signal_carrier all[3];
    int i, k;

    all[0] = bare();
    all[1] = modulated();
    all[2] = pulsed();
    for (i = 0; i < 3; i++) {
        /* The extremes of every number that reaches a sentence: a fraction
           that rounds to 100, a height in three digits, and counts that do. */
        struct signal_carrier c = all[i];
        struct signal_findings f;
        double duties[3] = { 1.0, 0.05, 0.5 };
        int d, e, v;
        const struct signal_envelope **envelope_cases;
        /*
         * And every burst verdict, because a sentence is truncated by how
         * wide it is and the burst lines carry three numbers of their own.
         * Leaving them out of this sweep is the gap that let the first
         * findings ship truncated.
         */
        struct signal_bursts envelopes[4];
        const struct signal_bursts *b[4];

        memset(envelopes, 0, sizeof(envelopes));
        envelopes[1].verdict = SIGNAL_BURST_LEVEL;
        envelopes[2].verdict = SIGNAL_BURST_BUSY;
        envelopes[2].occupancy = 0.999;
        envelopes[3].verdict = SIGNAL_BURST_SEPARABLE;
        envelopes[3].count = 9999;
        envelopes[3].median_seconds = 99.9;      /* absurd, on purpose */
        envelopes[3].median_gap_seconds = 999.9;
        envelopes[3].occupancy = 0.9999;
        b[0] = NULL;
        b[1] = &envelopes[1];
        b[2] = &envelopes[2];
        b[3] = &envelopes[3];

        c.carrier_over_noise_db = 100.0;
        c.carrier_power_fraction = 1.0;
        {
            /* Every envelope branch too, at absurd values, for the same
               reason the burst ones are here: a sentence is truncated by how
               wide it is. */
            static struct signal_envelope env[3];
            static const struct signal_envelope *ev[3];
            memset(env, 0, sizeof(env));
            env[1].found = 1;
            env[1].variation = 0.001;
            env[2].found = 1;
            env[2].variation = 999.999;
            ev[0] = NULL; ev[1] = &env[1]; ev[2] = &env[2];
            envelope_cases = ev;
        }
        for (d = 0; d < 3; d++)
            for (e = 0; e < 4; e++)
            for (v = 0; v < 3; v++) {
                signal_findings_from(&c, b[e], envelope_cases[v], duties[d],
                                     999, 999, 99900.0, &f);
                check_msg(f.count <= SIGNAL_FINDING_LINES,
                          "carrier %d duty %d bursts %d envelope %d overran: "
                          "%d lines", i, d, e, v, f.count);
                for (k = 0; k < f.count; k++)
                    check_msg(strlen(f.line[k]) <= SIGNAL_FINDING_FIT,
                              "carrier %d duty %d bursts %d envelope %d line "
                              "%d fits: %zu of %d -- \"%s\"",
                              i, d, e, v, k, strlen(f.line[k]),
                              SIGNAL_FINDING_FIT, f.line[k]);
            }
    }
    {
        struct signal_findings f;
        signal_findings_from(NULL, NULL, NULL, 0.0, 0, 0, 0.0, &f);
        check_true("the refusal fits too",
                   strlen(f.line[0]) <= SIGNAL_FINDING_FIT);
    }
}

int main(void) {
    test_a_bare_carrier_says_there_is_nothing_to_decode();
    test_a_modulated_carrier_says_what_was_not_asked();
    test_a_pulse_is_not_a_bare_tone();
    test_the_refusals_are_kept();
    test_duty_and_drift_qualify_the_claim();
    test_it_never_overruns();
    test_the_envelope_is_named_only_where_it_separates();
    test_every_branch_fits_the_panel();
    return check_report("what the candidate panel concludes, and refuses to");
}
