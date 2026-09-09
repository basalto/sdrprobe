/*
 * check-survey-session -- the band survey's state machine, with no window, no
 * receiver and no process launch.
 *
 * `.scratch/deepening/issues/04-survey-session.md`. What moved into
 * `survey_session.{c,h}` is the part of the survey that *decides*: when a step
 * is over, which block is stale, what a confirmation pass asks about, what it
 * concludes from six looks, and what a watch reports as appeared and gone.
 * Every one of those used to sit in `view_survey.c` beside the drawing, with a
 * second copy in `survey_report.c`, and the only way to reach any of them was
 * to run the built program against a dongle (ADR-0012).
 *
 * `check-pipelines` stays and is not replaced: it proves the session is wired
 * into the program, which this cannot. What this proves is that the machine is
 * right, in milliseconds.
 *
 * The captures go through the shipping converter and the shipping transform,
 * so a candidate here is the candidate the program reports. The assertions on
 * `gsm_arfcn_69.bin` are the ones `check-pipelines` makes over stdout.
 */

#include "check.h"

#include "survey_session.h"
#include "sdr_dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* SAMPLE_BLOCK_PAIRS, which is dump1090's block in pairs and not in bytes
   -- .scratch/device-model/issues/09-*. */
#define BLOCK_PAIRS (8 * 16384)
#define RATE 2000000.0

static uint8_t raw[BLOCK_PAIRS * 2];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
static float scratch[BLOCK_PAIRS];
static float spectrum[SDR_DSP_FFT_MAX];
static float spectrum_max[SDR_DSP_FFT_MAX];
static struct sdr_dsp dsp;

/*
 * One block of a capture, as the program prepares it: convert, remove the DC
 * the receiver adds, transform. `--remove-dc` defaults on, so this is what a
 * survey of a capture actually sees.
 */
static int block_from_capture(FILE *f, double centre_hz,
                              const struct device_profile *profile,
                              struct survey_block *out) {
    size_t got = fread(raw, 1, sizeof(raw), f);
    size_t pairs;

    if (got < sizeof(raw))
        return 0;               /* whole blocks only, so a run repeats */
    pairs = sdr_dsp_convert_iq(profile, raw, got, I, Q, M, BLOCK_PAIRS);
    if (pairs == 0)
        return 0;
    sdr_dsp_remove_dc(I, Q, pairs);
    if (sdr_dsp_spectrum(&dsp, I, Q, pairs, SDR_DSP_FFT_SIZE,
                         profile->full_scale, spectrum, spectrum_max) <= 0)
        return 0;

    memset(out, 0, sizeof(*out));
    out->i_samples = I;
    out->q_samples = Q;
    out->pair_count = pairs;
    out->spectrum = spectrum;
    out->scratch = scratch;
    out->centre_hz = centre_hz;
    out->sample_rate = RATE;
    out->reference_clock_hz = 0.0;   /* a capture has no crystal to blame */
    out->remove_dc = 1;
    return 1;
}

/*
 * Survey a capture through the session, exactly as `--headless --survey` does:
 * one tuning, every block folded, finished when the file runs out.
 */
static int survey_capture(const char *path, double centre_hz,
                          struct survey_session *ss) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8,
                               device_default_full_scale(SAMPLE_FORMAT_U8),
                               0.0, 2000000);
    struct survey_session_event event;
    struct survey_block block;
    FILE *f = fopen(path, "rb");

    if (!f)
        return 0;
    survey_session_reset(ss);
    if (survey_session_one_tuning(ss, centre_hz, RATE, &event) !=
        SURVEY_PLAN_OK) {
        fclose(f);
        return 0;
    }
    while (block_from_capture(f, centre_hz, &profile, &block))
        survey_session_tick(ss, &block, 1, 0.0, &event);
    fclose(f);
    survey_session_source_ended(ss, NULL, &event);
    return event.sweep_finished;
}

/*
 * A capture surveyed twice is the same survey.
 *
 * This is the invariant `check-pipelines` asserts by running the program twice
 * and diffing stdout, and the reason it matters is not paranoia about
 * determinism: a survey an agent cannot diff against yesterday's is a survey
 * it cannot use to notice anything. Here it is reachable without a process,
 * and it also pins that the session carries nothing between runs -- the peaks,
 * the carriers and the marks are all cleared by arming.
 */
static void test_the_same_capture_twice(void) {
    static struct survey_session first, second;
    int i;

    if (!survey_capture("testfiles/gsm_arfcn_69.bin", 948.4e6, &first)) {
        check_true("testfiles/gsm_arfcn_69.bin surveys", 0);
        return;
    }
    /* 31 whole blocks of 131072 pairs, all folded, none discarded: one tuning
       never retunes, so no block it delivers is stale. */
    check_int("31 blocks folded", first.blocks_folded, 31);
    check_int("and none discarded as settling", first.blocks_discarded, 0);
    check_int("one step", first.plan.step_count, 1);

    /* ARFCN 69's carrier and ARFCN 63 beside it, as check-pipelines pins. */
    check_int("seven candidates", first.peak_count, 7);
    check_int("which are two carriers", first.carrier_count, 2);
    check_true("the strongest is ARFCN 69's",
               first.carriers[0].centre_hz > 948.6e6 &&
                   first.carriers[0].centre_hz < 948.9e6);

    check_true("and again", survey_capture("testfiles/gsm_arfcn_69.bin",
                                           948.4e6, &second) != 0);
    check_int("the same candidates", second.peak_count, first.peak_count);
    check_int("the same carriers", second.carrier_count, first.carrier_count);
    for (i = 0; i < first.peak_count && i < second.peak_count; i++)
        check_msg(second.peaks[i].index == first.peaks[i].index &&
                      second.peaks[i].power_dbfs == first.peaks[i].power_dbfs,
                  "candidate %d moved: bin %d at %.2f, was bin %d at %.2f\n",
                  i, second.peaks[i].index, (double)second.peaks[i].power_dbfs,
                  first.peaks[i].index, (double)first.peaks[i].power_dbfs);
}

/*
 * The Mode S capture, which is here because it nearly was not reported at all.
 *
 * "Mode S is pulses: there is no carrier standing above anything" was false --
 * a train of pulses is amplitude modulation on a carrier -- and the survey
 * still reported nothing, because that carrier has no -20 dB point of its own
 * and the width walk was unbounded.
 */
static void test_the_mode_s_carrier(void) {
    static struct survey_session ss;

    if (!survey_capture("testfiles/adsb_cpr_pair.bin", 1090e6, &ss)) {
        check_true("testfiles/adsb_cpr_pair.bin surveys", 0);
        return;
    }
    check_true("at least one candidate", ss.peak_count >= 1);
    if (ss.peak_count >= 1) {
        double hz = survey_session_bin_hz(&ss, ss.peaks[0].index);
        check_msg(hz > 1089.9e6 && hz < 1090.1e6,
                  "the candidate is at %.0f Hz, not 1090 MHz\n", hz);
    }
}

/*
 * A capture holds one tuning, so the spectrum it peak-held over the whole file
 * is a spectrum every candidate can be measured out of. A swept range's is
 * not: it belongs to whichever step happened to be last, and a bandwidth read
 * from it is a number about the wrong signal.
 */
static void test_which_spectrum_may_be_measured(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    if (!survey_capture("testfiles/gsm_arfcn_69.bin", 948.4e6, &ss)) {
        check_true("testfiles/gsm_arfcn_69.bin surveys", 0);
        return;
    }
    check_true("one tuning offers its held spectrum",
               survey_session_spectrum(&ss) != NULL);

    survey_session_reset(&ss);
    check_int("a sweep of 200 MHz plans many steps",
              survey_session_sweep(&ss, 800e6, 1000e6, RATE, 0.10, 0.0,
                                   &event),
              SURVEY_PLAN_OK);
    check_true("which is more than one step", ss.plan.step_count > 1);
    check_true("and offers no spectrum to measure from",
               survey_session_spectrum(&ss) == NULL);
}

/* ------------------------------------------------------------------ *
 * The sweep, on a synthetic signal
 * ------------------------------------------------------------------ */

/*
 * A flat floor with one carrier in it, at a bin of the caller's choosing.
 *
 * Synthetic on purpose: a sweep needs a *receiver* to walk, and there is no
 * capture of one. What is under test here is the step machine -- which block
 * is folded, which is thrown away, when the step advances, where the tuning is
 * asked to go -- and that is arithmetic over a spectrum, whatever produced it.
 */
static void flat_spectrum(float floor_dbfs) {
    int i;
    for (i = 0; i < SDR_DSP_FFT_SIZE; i++)
        spectrum[i] = floor_dbfs;
}

static void put_carrier(double centre_hz, double at_hz, float dbfs) {
    double bin_hz = RATE / (double)SDR_DSP_FFT_SIZE;
    double lower = centre_hz - RATE / 2.0;
    int bin = (int)((at_hz - lower) / bin_hz);
    int i;

    if (bin < 2 || bin >= SDR_DSP_FFT_SIZE - 2)
        return;
    for (i = bin - 1; i <= bin + 1; i++)
        spectrum[i] = dbfs;
}

static struct survey_block synthetic_block(double centre_hz) {
    struct survey_block block;

    memset(&block, 0, sizeof(block));
    block.i_samples = I;
    block.q_samples = Q;
    block.pair_count = 4096;
    block.spectrum = spectrum;
    block.scratch = scratch;
    block.centre_hz = centre_hz;
    block.sample_rate = RATE;
    block.reference_clock_hz = 0.0;
    return block;
}

/*
 * The settle is the whole of this test.
 *
 * A block that arrives before SURVEY_SETTLE_SECONDS is over was in the
 * pipeline while the tuner was moving, so it holds the *previous* step's
 * samples. Folding it writes that step's signal into this step's bins, at a
 * frequency nothing is transmitting on -- a real carrier drawn where there is
 * none. The headless sweep folded every block it consumed until this machine
 * existed, which at a 0.10 s settle and a 0.10 s dwell was about a third of
 * everything it measured.
 */
static void test_a_stale_block_is_not_folded(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;
    double first_centre;

    survey_session_reset(&ss);
    check_int("a two-step sweep plans",
              survey_session_sweep(&ss, 900e6, 903e6, RATE, 0.10, 0.0,
                                   &event),
              SURVEY_PLAN_OK);
    check_int("in two steps", ss.plan.step_count, 2);
    check_true("and asks for a tuning", event.retune_hz > 0);
    first_centre = (double)event.retune_hz;

    /* A loud carrier, in a block that arrives while the tuner is still
       moving. It must leave no mark. */
    flat_spectrum(-90.0f);
    put_carrier(first_centre, first_centre, -20.0f);
    block = synthetic_block(first_centre);
    survey_session_tick(&ss, &block, 1, 0.05, &event);
    check_int("nothing folded during the settle", ss.blocks_folded, 0);
    check_int("it was counted as discarded", ss.blocks_discarded, 1);
    check_int("no candidates from it", ss.peak_count, 0);

    /* The same block after the settle is folded, and found. */
    survey_session_tick(&ss, &block, 1, 0.15, &event);
    check_int("folded once the tuner has caught up", ss.blocks_folded, 1);
    check_true("and the carrier is a candidate", ss.peak_count >= 1);
}

/*
 * A step does not end until something was heard on it.
 *
 * The dwell being over is a floor on how long to listen, not a promise that
 * anything arrived: blocks come every 65.5 ms and the settle takes the first
 * of them, so a step with a short dwell can pass through both phases having
 * folded nothing -- and the bins it was responsible for are left unmeasured,
 * which draws as a gap and reads as a band with nothing in it.
 */
static void test_a_step_waits_for_one_block(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;

    survey_session_reset(&ss);
    survey_session_sweep(&ss, 900e6, 903e6, RATE, 0.02, 0.0, &event);
    check_true("more than one step", ss.plan.step_count > 1);

    /* Long past the dwell, and nothing has arrived. */
    survey_session_tick(&ss, NULL, 0, 5.0, &event);
    check_int("no retune while the step has heard nothing", (long)event.retune_hz,
              0);
    check_int("still on the first step", ss.step, 0);

    flat_spectrum(-90.0f);
    block = synthetic_block(survey_plan_step_centre(&ss.plan, 0));
    survey_session_tick(&ss, &block, 1, 5.1, &event);
    check_true("one block folded moves it on", event.retune_hz > 0);
    check_int("to the second step", ss.step, 1);
}

/*
 * A sweep of one step is over as soon as it has folded a block, and it hands
 * the receiver back rather than leaving it wherever the last step put it.
 */
static void test_a_sweep_finishes_and_lets_go(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;
    double centre;

    survey_session_reset(&ss);
    /* One step of a 2 MS/s receiver covers SURVEY_USABLE_SPAN of its span --
       1.6 MHz -- and its tuning sits in the middle of that, which is above
       the middle of a range narrower than a step. A carrier has to be inside
       the *plan's* range to reach a bin at all. */
    survey_session_sweep(&ss, 900e6, 901.6e6, RATE, 0.10, 0.0, &event);
    check_int("one step covers 1.6 MHz at 2 MS/s", ss.plan.step_count, 1);
    centre = survey_plan_step_centre(&ss.plan, 0);
    check_close("tuned to the middle of it", centre, 900.8e6, 1.0);

    flat_spectrum(-90.0f);
    put_carrier(centre, centre + 300e3, -30.0f);
    block = synthetic_block(centre);
    check_true("sweeping", survey_session_sweeping(&ss) != 0);
    survey_session_tick(&ss, &block, 1, 0.15, &event);
    check_true("still sweeping inside the dwell",
               survey_session_sweeping(&ss) != 0);
    survey_session_tick(&ss, &block, 1, 0.35, &event);
    check_true("finished after the dwell", event.sweep_finished != 0);
    check_true("and no longer sweeping", survey_session_sweeping(&ss) == 0);
    check_true("the receiver is handed back", event.release_receiver != 0);
    check_int("with no further tuning asked for", (long)event.retune_hz, 0);
    check_true("the candidate survived", ss.peak_count >= 1);
}

/*
 * A refused tuning stops whatever was running and says where it would not go.
 *
 * The session never touches the receiver, so a refusal has to come back the
 * other way. Without this the machine would sit on a step for ever waiting
 * for blocks from a tuning that never happened.
 */
static void test_a_refused_tuning_stops_it(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    survey_session_reset(&ss);
    survey_session_sweep(&ss, 900e6, 903e6, RATE, 0.10, 0.0, &event);
    check_true("sweeping", survey_session_sweeping(&ss) != 0);
    survey_session_retune_failed(&ss, 900.8e6, &event);
    check_true("stopped", survey_session_sweeping(&ss) == 0);
    check_true("reported as stopped", event.sweep_stopped != 0);
    check_true("the receiver is handed back", event.release_receiver != 0);
    check_true("and it says which frequency",
               strstr(ss.status, "900.8000") != NULL);
}

/* ------------------------------------------------------------------ *
 * Asking again
 * ------------------------------------------------------------------ */

/* A sweep of one step with two carriers in it, so there is something to ask
   about. Leaves the session idle with peaks and carriers filled in. */
static void swept_two_carriers(struct survey_session *ss, double *centre_out) {
    struct survey_session_event event;
    struct survey_block block;
    double centre;

    survey_session_reset(ss);
    survey_session_sweep(ss, 900e6, 901.6e6, RATE, 0.10, 0.0, &event);
    centre = survey_plan_step_centre(&ss->plan, 0);
    flat_spectrum(-90.0f);
    put_carrier(centre, centre - 300e3, -30.0f);
    put_carrier(centre, centre + 300e3, -35.0f);
    block = synthetic_block(centre);
    survey_session_tick(ss, &block, 1, 0.15, &event);
    survey_session_tick(ss, &block, 1, 0.35, &event);
    if (centre_out)
        *centre_out = centre;
}

/*
 * Six looks, and the three verdicts that come out of counting them.
 *
 * `confirmed` when it was up in every look, `refuted` when in none, and
 * **`intermittent`** in between -- which is the answer the mobile-satellite
 * bands need: on 1600-1670 MHz a pass put four of seven signals there, and
 * every one of them would previously have been refuted and barred from the
 * site history for ever.
 *
 * Driven through the machine rather than through `survey_confirm.h`'s pure
 * rules, which have their own suite: what is under test is that six ticks
 * produce six looks, that the settle is honoured before the first of them, and
 * that the count of hits reaches the verdict.
 */
static void confirm_one_target(struct survey_session *ss, int present_looks) {
    struct survey_session_event event;
    struct survey_block block;
    double target_hz = ss->confirm.target[ss->confirm.index].hz;
    double centre = target_hz - SURVEY_CONFIRM_OFFSET_HZ;
    double now = ss->confirm.started_at;
    int look;

    block = synthetic_block(centre);
    /* The settle first: a block arriving inside it is the previous target's
       spectrum and is not a look at this one. */
    now += SURVEY_CONFIRM_SETTLE_SECONDS + 0.01;
    survey_session_tick(ss, &block, 0, now, &event);
    for (look = 0; look < SURVEY_CONFIRM_LOOKS; look++) {
        flat_spectrum(-90.0f);
        if (look < present_looks)
            put_carrier(centre, target_hz, -30.0f);
        now += 0.07;
        survey_session_tick(ss, &block, 1, now, &event);
    }
}

static void test_the_three_verdicts(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    int asked;

    swept_two_carriers(&ss, NULL);
    asked = survey_session_confirm_all(&ss, 10.0, &event);
    check_int("both carriers are asked about", asked, 2);
    check_true("and the pass is running", survey_session_confirming(&ss) != 0);
    check_true("it asks for a tuning below the first target",
               (double)event.retune_hz <
                   ss.confirm.target[0].hz - SURVEY_CONFIRM_OFFSET_HZ / 2.0);

    /* Up in every look: confirmed. */
    confirm_one_target(&ss, SURVEY_CONFIRM_LOOKS);
    check_int("six looks were taken", ss.confirm.target[0].looks,
              SURVEY_CONFIRM_LOOKS);
    check_int("all six found it", ss.confirm.target[0].hits,
              SURVEY_CONFIRM_LOOKS);
    check_int("confirmed", ss.confirm.target[0].verdict,
              SURVEY_VERDICT_CONFIRMED);
    check_int("and it moved to the second target", ss.confirm.index, 1);

    /* Up in two of six: intermittent, which is a finding and not a mistake. */
    confirm_one_target(&ss, 2);
    check_int("two of six", ss.confirm.target[1].hits, 2);
    check_int("intermittent", ss.confirm.target[1].verdict,
              SURVEY_VERDICT_INTERMITTENT);
    check_true("the pass is over", survey_session_confirming(&ss) == 0);
    check_int("one confirmed", ss.confirm.confirmed, 1);
    check_int("one intermittent", ss.confirm.intermittent, 1);
    check_int("none refuted", ss.confirm.refuted, 0);

    /* And a target nothing was ever found at is refuted. */
    swept_two_carriers(&ss, NULL);
    survey_session_confirm_all(&ss, 10.0, &event);
    confirm_one_target(&ss, 0);
    check_int("no look found it", ss.confirm.target[0].hits, 0);
    check_int("refuted", ss.confirm.target[0].verdict,
              SURVEY_VERDICT_REFUTED);
    check_true("and it is reported as never measured, not as measured at 0",
               ss.confirm.target[0].prominence_db < 0.1f);
}

/*
 * A pass gives up part way through, and the verdicts already reached stand.
 *
 * Throwing them away because the operator pressed the button again would lose
 * the only evidence the sweep's claims ever get.
 */
static void test_abandoning_keeps_what_was_settled(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    swept_two_carriers(&ss, NULL);
    survey_session_confirm_all(&ss, 10.0, &event);
    confirm_one_target(&ss, SURVEY_CONFIRM_LOOKS);
    check_int("one settled", ss.confirm.index, 1);
    survey_session_confirm_abandon(&ss, &event);
    check_true("the pass is over", survey_session_confirming(&ss) == 0);
    check_true("reported as finished", event.confirm_finished != 0);
    check_int("the first verdict stands", ss.confirm.target[0].verdict,
              SURVEY_VERDICT_CONFIRMED);
    check_int("the second was never reached",
              ss.confirm.target[1].verdict, SURVEY_VERDICT_PENDING);
}

/*
 * Stopping stops a pass in flight as well as a sweep.
 *
 * Leaving the survey screen stops the machine, and a pass left running would
 * be one nothing ticks in front of a view nothing reaches -- the input handler
 * waits on a pass rather than fighting it for the receiver, so a pass that
 * cannot end is a screen that cannot be used.
 */
static void test_stopping_stops_a_pass_too(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    swept_two_carriers(&ss, NULL);
    survey_session_confirm_all(&ss, 10.0, &event);
    check_true("a pass is running", survey_session_confirming(&ss) != 0);
    survey_session_stop(&ss, &event);
    check_int("stopped", survey_session_confirming(&ss), 0);
    check_int("and it is not still marked as running", ss.confirm.running, 0);
    check_int("nothing is asked for", (long)event.retune_hz, 0);
}

/*
 * "Ask again" asks about what changed, and a sweep that matches what the site
 * has heard has nothing to ask.
 *
 * This is the distinction the two entry points exist for: the window has a
 * history to lean on and asks only about the changes, while a scripted sweep's
 * output *is* the report, so it asks about every signal it found. Above
 * 1.5 GHz that is most of the answer -- one 1400-1766 MHz sweep found ten
 * signals and the pass confirmed one.
 */
static void test_what_a_pass_asks_about(void) {
    static struct survey_session ss;
    struct site_history history;
    struct survey_session_event event;
    double hz[2];
    float level[2], prom[2];
    int i;

    swept_two_carriers(&ss, NULL);
    check_int("two carriers", ss.carrier_count, 2);

    /* No history: nothing is "new here", because "this site has never heard
       it" and "nobody asked" are different claims. */
    check_int("nothing to ask about without a history",
              survey_session_change_count(&ss), 0);
    check_int("so the changes pass refuses",
              survey_session_confirm_changes(&ss, 1.0, &event), -1);
    check_int("while the every-signal pass asks about both",
              survey_session_confirm_all(&ss, 1.0, &event), 2);
    survey_session_confirm_abandon(&ss, &event);

    /*
     * A history with a sweep in it, of somewhere else. Now the two carriers
     * are new *here*, which is a claim only a site that has listened before
     * can make -- an empty history says `unknown`, not `new`, because
     * "this site has never heard it" and "nobody has ever listened" are
     * different statements and only the first is worth asking about.
     *
     * The decoy sits outside the range so it is not also reported absent: a
     * sweep of one band says nothing about a signal three hundred megahertz
     * away.
     */
    site_history_init(&history, "a-room");
    hz[0] = 800e6;
    level[0] = -40.0f;
    prom[0] = 20.0f;
    site_history_merge(&history, hz, level, prom, 1, ss.plan.bin_hz, 12);
    survey_session_set_history(&ss, &history, 1);
    check_int("both are new to this site",
              survey_session_change_count(&ss), 2);
    check_int("and both are asked about",
              survey_session_confirm_changes(&ss, 1.0, &event), 2);
    for (i = 0; i < 2; i++)
        check_int("claimed as new", ss.confirm.target[i].claim,
                  SURVEY_CLAIM_NEW);
    survey_session_confirm_abandon(&ss, &event);

    /* Now teach the site both of them, and the same sweep has nothing to
       report. */
    for (i = 0; i < 2; i++) {
        hz[i] = ss.carriers[i].centre_hz;
        level[i] = ss.carriers[i].peak_dbfs;
        prom[i] = ss.carriers[i].prominence_db;
    }
    site_history_merge(&history, hz, level, prom, 2, ss.plan.bin_hz, 12);
    survey_session_set_history(&ss, &history, 1);
    check_int("both are known now", survey_session_change_count(&ss), 0);
    for (i = 0; i < 2; i++)
        check_int("marked as known", ss.carrier_status[i],
                  SITE_STATUS_KNOWN);
}

/*
 * An empty history marks nothing, and neither does no history at all.
 *
 * The distinction the marks rest on: `unknown` is "nobody has listened here",
 * `new` is "this site has listened and never heard it". A machine that
 * collapsed the two would open its first sweep of a fresh site by calling
 * every signal in the band a change, and a confirmation pass would then spend
 * minutes asking about all of them.
 */
static void test_a_fresh_site_marks_nothing(void) {
    static struct survey_session ss;
    struct site_history history;
    int i;

    swept_two_carriers(&ss, NULL);
    site_history_init(&history, "a-room");
    survey_session_set_history(&ss, &history, 1);
    check_int("nothing to ask about", survey_session_change_count(&ss), 0);
    for (i = 0; i < ss.carrier_count; i++)
        check_int("marked unknown, not new", ss.carrier_status[i],
                  SITE_STATUS_UNKNOWN);
    check_int("and nothing is absent", ss.missing_count, 0);
}

/* ------------------------------------------------------------------ *
 * Watching
 * ------------------------------------------------------------------ */

/*
 * A watch with nowhere to put what it learns is refused.
 *
 * Not a nicety. A watch that only looked would learn nothing -- every sweep
 * would find the same signals "new", because new means "this site has not
 * heard it" and nothing would ever be written down -- so it would sweep for
 * hours and produce a report identical to one sweep.
 */
static void test_a_watch_needs_a_site(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    swept_two_carriers(&ss, NULL);
    check_int("refused without a site",
              survey_session_watch(&ss, 0, 0, 0.0, &event), -1);
    check_int("and not watching", ss.watching, 0);
    check_true("it says the site is what is missing",
               strstr(ss.status, "site") != NULL);

    /* And with a site, but nothing swept yet, it says so instead: a watch
       repeats a range, and there is no range until one has been swept. */
    survey_session_reset(&ss);
    check_int("refused with no range", survey_session_watch(&ss, 1, 0, 0.0, &event),
              -1);
    check_int("still not watching", ss.watching, 0);
}

/*
 * A watch of two sweeps folds each into the history and says what changed.
 *
 * The folding is what makes the history worth having, and the counts are what
 * the run reports: the first sweep of a fresh site finds everything new, and
 * the second -- of the same signals -- finds nothing new, which is the whole
 * of "what changed while nobody was looking".
 */
static void test_a_watch_reports_what_changed(void) {
    static struct survey_session ss;
    struct site_history history;
    struct survey_session_event event;
    struct survey_block block;
    double centre;
    double now = 1.0;
    int sweep;

    swept_two_carriers(&ss, &centre);
    site_history_init(&history, "a-room");
    survey_session_set_history(&ss, &history, 1);

    check_int("the watch begins",
              survey_session_watch(&ss, 1, 2, now, &event), 0);
    check_true("and it re-arms the sweep", event.retune_hz > 0);

    for (sweep = 0; sweep < 2; sweep++) {
        flat_spectrum(-90.0f);
        put_carrier(centre, centre - 300e3, -30.0f);
        put_carrier(centre, centre + 300e3, -35.0f);
        block = synthetic_block(centre);
        now += 0.15;
        survey_session_tick(&ss, &block, 1, now, &event);
        now += 0.25;
        survey_session_tick(&ss, &block, 1, now, &event);
        check_msg(event.sweep_finished != 0, "sweep %d did not finish\n",
                  sweep + 1);
        check_msg(event.watch_swept != 0, "sweep %d was not folded in\n",
                  sweep + 1);
        check_msg(event.history_dirty != 0,
                  "sweep %d did not ask for the history to be written\n",
                  sweep + 1);
        if (sweep == 0) {
            check_int("the first sweep of a fresh site finds both new",
                      ss.watch_appeared, 2);
            /* And says how many it found, which is not `carrier_count` by
               the time anything reads it: going round again clears that. */
            check_int("and reports the two it found", ss.watch_carriers, 2);
            check_int("the array is cleared for the next sweep",
                      ss.carrier_count, 0);
            check_true("and goes round again", event.retune_hz > 0);
            check_int("without letting the receiver go",
                      event.release_receiver, 0);
        } else {
            check_int("the second finds nothing new", ss.watch_appeared, 0);
            check_int("nothing has gone quiet either", ss.watch_lost, 0);
        }
        now += 0.05;
    }
    check_int("two sweeps", ss.watch_sweeps, 2);
    check_int("two appeared over the watch", ss.watch_total_appeared, 2);
    check_true("the watch is over", ss.watching == 0);
    check_true("and said so", event.watch_finished != 0);
    check_true("and handed the receiver back", event.release_receiver != 0);
}

/*
 * A signal the watch stops hearing goes quiet.
 *
 * The count that answers "what changed while nobody was looking" in the other
 * direction, and the one a single sweep can never reach.
 */
static void test_a_watch_notices_silence(void) {
    static struct survey_session ss;
    struct site_history history;
    struct survey_session_event event;
    struct survey_block block;
    double centre;
    double now = 1.0;

    swept_two_carriers(&ss, &centre);
    site_history_init(&history, "a-room");
    survey_session_set_history(&ss, &history, 1);
    survey_session_watch(&ss, 1, 0, now, &event);

    /* One sweep with both carriers, so the site knows both. */
    flat_spectrum(-90.0f);
    put_carrier(centre, centre - 300e3, -30.0f);
    put_carrier(centre, centre + 300e3, -35.0f);
    block = synthetic_block(centre);
    now += 0.15;
    survey_session_tick(&ss, &block, 1, now, &event);
    now += 0.25;
    survey_session_tick(&ss, &block, 1, now, &event);
    check_int("both are new the first time", ss.watch_appeared, 2);

    /* And one with only the loud one. */
    flat_spectrum(-90.0f);
    put_carrier(centre, centre - 300e3, -30.0f);
    now += 0.20;
    survey_session_tick(&ss, &block, 1, now, &event);
    now += 0.25;
    survey_session_tick(&ss, &block, 1, now, &event);
    check_int("nothing new", ss.watch_appeared, 0);
    check_int("and one went quiet", ss.watch_lost, 1);
    check_int("over the watch, one lost", ss.watch_total_lost, 1);
}

/* ------------------------------------------------------------------ *
 * Measuring one candidate
 * ------------------------------------------------------------------ */

/*
 * Measuring a candidate has the same settle a sweep step does, and did not.
 *
 * Selecting a candidate retunes the receiver, and the blocks already in the
 * pipeline hold the previous tuning's samples -- so peak power, prominence,
 * bandwidth and duty were all being computed partly from wherever the receiver
 * had just been. A spectrum *average* blurs one stale block among the good
 * ones well enough that it never showed; a carrier measurement cannot, and the
 * first live run called the 75.000 MHz clock harmonic "a modulated carrier,
 * 19 dB up" where the same signal recorded and measured offline reads 40.7 dB.
 */
static void test_measuring_waits_for_the_tuner(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;
    double centre, target;

    swept_two_carriers(&ss, &centre);
    target = ss.carriers[0].centre_hz;
    survey_session_measure(&ss, target, 0.0, &event);
    check_true("measuring", survey_session_measuring(&ss) != 0);
    check_int("tuned below the candidate, clear of the DC spike",
              (long)event.retune_hz,
              (long)llround(target - SURVEY_OFFSET_HZ));

    /* A block inside the settle is the previous tuning's. */
    flat_spectrum(-90.0f);
    put_carrier(target - SURVEY_OFFSET_HZ, target, -30.0f);
    block = synthetic_block(target - SURVEY_OFFSET_HZ);
    survey_session_tick(&ss, &block, 1, 0.05, &event);
    check_int("nothing measured yet", ss.measure.blocks, 0);
    check_int("and no report", ss.report_valid, 0);

    survey_session_tick(&ss, &block, 1, 0.20, &event);
    check_int("one block measured", ss.measure.blocks, 1);
    check_int("the candidate was up in it", ss.measure.hits, 1);
    check_true("so there is a report", ss.report_valid != 0);

    /* And it stops on its own after SURVEY_MEASURE_SECONDS. */
    survey_session_tick(&ss, &block, 1, SURVEY_MEASURE_SECONDS + 0.1, &event);
    check_true("finished", event.measure_finished != 0);
    check_true("no longer measuring", survey_session_measuring(&ss) == 0);
}

/*
 * A candidate that cannot be measured forgets the measurement and keeps the
 * sweep.
 *
 * A capture holds one tuning, so nothing can be pointed at anything and
 * selecting says so -- but the candidates are what the sweep found, and a
 * click that emptied the list it was a click *in* would be the worst kind of
 * wrong. The first extraction of this machine did exactly that, by reaching
 * for the wrong reset.
 */
static void test_forgetting_a_measurement_keeps_the_sweep(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;
    double centre, target;
    int peaks, carriers;

    swept_two_carriers(&ss, &centre);
    peaks = ss.peak_count;
    carriers = ss.carrier_count;
    check_true("there are candidates to select from", peaks > 0);

    /* Measure one, then throw the measurement away. */
    target = ss.carriers[0].centre_hz;
    survey_session_measure(&ss, target, 0.0, &event);
    flat_spectrum(-90.0f);
    put_carrier(target - SURVEY_OFFSET_HZ, target, -30.0f);
    block = synthetic_block(target - SURVEY_OFFSET_HZ);
    survey_session_tick(&ss, &block, 1, 0.20, &event);
    check_true("measured", ss.report_valid != 0);

    survey_session_forget_measurement(&ss);
    check_int("the report is gone", ss.report_valid, 0);
    check_int("and the blocks with it", ss.measure.blocks, 0);
    check_int("no longer measuring", survey_session_measuring(&ss), 0);
    check_int("the candidates stay", ss.peak_count, peaks);
    check_int("and so do the carriers", ss.carrier_count, carriers);
}

/*
 * A sweep clears what the last one measured.
 *
 * A different range is a different measurement, and drawing one range's peaks
 * under another's axis puts every one of them at a frequency it was not
 * measured at. The verdicts go with them: leaving the last sweep's would
 * attach them to whatever this sweep finds at those frequencies, which is a
 * claim nobody made.
 */
static void test_a_new_sweep_forgets_the_last(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    swept_two_carriers(&ss, NULL);
    survey_session_confirm_all(&ss, 10.0, &event);
    confirm_one_target(&ss, SURVEY_CONFIRM_LOOKS);
    survey_session_confirm_abandon(&ss, &event);
    check_true("there are peaks", ss.peak_count > 0);
    check_true("and verdicts", ss.confirm.count > 0);

    survey_session_sweep(&ss, 400e6, 401.6e6, RATE, 0.10, 0.0, &event);
    check_int("the peaks are gone", ss.peak_count, 0);
    check_int("the carriers with them", ss.carrier_count, 0);
    check_int("and the verdicts", ss.confirm.count, 0);
    check_int("nothing is confirmed", ss.confirm.confirmed, 0);
}

/*
 * Undoing a narrowing sweep, which is Reset zoom's whole job.
 *
 * Sweeping the window throws away everything outside it and getting that back
 * costs minutes, so the sweep is kept and put back instead. The order inside
 * the restore is the load-bearing part and it has been wrong twice: once
 * restoring the range and not the chart, and once **clearing the sweep it had
 * just put back** -- forgetting the measurement after the copy rather than
 * before it, which empties the very thing the button exists to recover.
 */
static void test_undoing_a_narrowing_sweep(void) {
    static struct survey_session ss;
    struct survey_session_event event;
    struct survey_block block;
    double wide_centre, wide_lower, wide_upper;
    double wide_bin_hz;
    int wide_peaks, wide_carriers;

    /* A wide sweep with two carriers in it. */
    swept_two_carriers(&ss, &wide_centre);
    wide_peaks = ss.peak_count;
    wide_carriers = ss.carrier_count;
    wide_lower = ss.lower_hz;
    wide_upper = ss.upper_hz;
    wide_bin_hz = ss.plan.bin_hz;
    check_true("the wide sweep found something", wide_peaks > 0);
    check_int("nothing kept yet", survey_session_has_kept(&ss), 0);

    survey_session_keep(&ss);
    check_true("kept", survey_session_has_kept(&ss) != 0);

    /* Now narrow it to a slice with nothing in it, as Sweep-the-window does. */
    survey_session_sweep(&ss, wide_centre + 500e3, wide_centre + 700e3, RATE,
                         0.10, 0.0, &event);
    flat_spectrum(-90.0f);
    block = synthetic_block(survey_plan_step_centre(&ss.plan, 0));
    survey_session_tick(&ss, &block, 1, 0.15, &event);
    survey_session_tick(&ss, &block, 1, 0.35, &event);
    check_int("the narrow sweep found nothing", ss.peak_count, 0);
    check_true("and the kept sweep survived it",
               survey_session_has_kept(&ss) != 0);

    /* And put it back. */
    check_int("restored", survey_session_restore(&ss), 0);
    check_int("the candidates are back", ss.peak_count, wide_peaks);
    check_int("and the carriers with them", ss.carrier_count, wide_carriers);
    check_close("the range is the wide one", ss.lower_hz, wide_lower, 1.0);
    check_close("both edges", ss.upper_hz, wide_upper, 1.0);
    /*
     * And the plan, which is what everything downstream is scaled by: the
     * carrier grouping, the history's matching tolerance, whether an extent
     * is a measurement or the instrument's floor. Restoring the bins without
     * it left a wide sweep's peaks read with a narrow sweep's bin width.
     */
    check_close("and the bin width that goes with it", ss.plan.bin_hz,
                wide_bin_hz, 1e-6);

    check_int("the kept copy is spent", survey_session_has_kept(&ss), 0);
    check_int("and restoring again refuses", survey_session_restore(&ss), -1);
}

/* A plan that cannot be made is refused in words, and leaves the machine
   idle rather than sweeping a range it does not have. */
static void test_a_range_it_will_not_sweep(void) {
    static struct survey_session ss;
    struct survey_session_event event;

    survey_session_reset(&ss);
    check_int("the high edge must be above the low one",
              survey_session_sweep(&ss, 900e6, 900e6, RATE, 0.10, 0.0, &event),
              SURVEY_PLAN_BAD_RANGE);
    check_true("and it is not sweeping", survey_session_sweeping(&ss) == 0);
    check_int("nor asking for a tuning", (long)event.retune_hz, 0);

    check_int("a dwell outside the bounds is refused",
              survey_session_sweep(&ss, 900e6, 903e6, RATE,
                                   SURVEY_DWELL_MAX + 1.0, 0.0, &event),
              SURVEY_PLAN_BAD_DWELL);
    check_true("still idle", survey_session_sweeping(&ss) == 0);
}

int main(void) {
    test_the_same_capture_twice();
    test_the_mode_s_carrier();
    test_which_spectrum_may_be_measured();
    test_a_stale_block_is_not_folded();
    test_a_step_waits_for_one_block();
    test_a_sweep_finishes_and_lets_go();
    test_a_refused_tuning_stops_it();
    test_the_three_verdicts();
    test_abandoning_keeps_what_was_settled();
    test_stopping_stops_a_pass_too();
    test_what_a_pass_asks_about();
    test_a_fresh_site_marks_nothing();
    test_a_watch_needs_a_site();
    test_a_watch_reports_what_changed();
    test_a_watch_notices_silence();
    test_measuring_waits_for_the_tuner();
    test_forgetting_a_measurement_keeps_the_sweep();
    test_a_new_sweep_forgets_the_last();
    test_undoing_a_narrowing_sweep();
    test_a_range_it_will_not_sweep();
    return check_report("the survey's machine: sweep, ask again, watch, "
                        "measure");
}
