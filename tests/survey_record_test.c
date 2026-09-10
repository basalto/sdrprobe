#include "check.h"

#include "survey_record.h"
#include "survey_suspect.h"

#include <string.h>

/*
 * What a finished survey is, before anybody draws it or writes it down.
 *
 * The claim this suite exists to make is one line and it is the whole of
 * ticket 12's hypothesis: **a complete survey can be formed from plain facts,
 * with no `struct app` anywhere.** `tests/survey_store_test.c` allocates one
 * to write a single file, which is what says the interface asks its callers
 * to know nearly everything; this file builds the same fixture without it.
 *
 * So the fixture below is deliberately the store check's fixture, value for
 * value -- two candidates at 94.492310 and 100.000000 MHz, one carrier
 * holding both, one confirmed target carrying the numbers a real pass
 * measured on the 75.000 MHz clock harmonic. When the JSON writer moves onto
 * the record (phase 4), the two can be asserted byte-identical against the
 * same numbers rather than against a second fixture, which is a second
 * fixture's worth of things to disagree about.
 */

static struct survey_plan a_plan(void) {
    struct survey_plan plan;

    memset(&plan, 0, sizeof(plan));
    plan.lower_hz = 88e6;
    plan.upper_hz = 108e6;
    plan.step_count = 13;
    plan.bins = 8192;
    plan.bin_hz = 2441.4;
    return plan;
}

static struct tm a_time(void) {
    struct tm when;

    memset(&when, 0, sizeof(when));
    when.tm_year = 126;    /* 2026 */
    when.tm_mon = 8;
    when.tm_mday = 10;
    when.tm_hour = 22;
    when.tm_min = 27;
    when.tm_sec = 31;
    return when;
}

static void the_candidates(struct survey_candidate *c) {
    memset(c, 0, 2 * sizeof(*c));
    c[0].found_hz = 94492310;
    c[0].power_dbfs = -7.7f;
    c[0].prominence_db = 35.9f;
    c[0].extent_hz = 200000.0;
    c[0].allocation = "FM broadcast";
    c[1].found_hz = 100000000;
    c[1].power_dbfs = -40.0f;
    c[1].prominence_db = 12.0f;
    c[1].extent_hz = 4882.8;
    c[1].suspect = SURVEY_SUSPECT_REFERENCE;
}

static struct survey_carrier the_carrier(void) {
    struct survey_carrier carrier;

    memset(&carrier, 0, sizeof(carrier));
    carrier.centre_hz = 94400000.0;
    carrier.power_centre_hz = 94410000.0;
    carrier.lower_hz = 94300000.0;
    carrier.upper_hz = 94500000.0;
    carrier.width_hz = 200000.0;
    carrier.peak_dbfs = -7.7f;
    carrier.prominence_db = 35.9f;
    carrier.peaks = 2;
    return carrier;
}

static struct survey_confirm_target the_target(void) {
    struct survey_confirm_target target;

    memset(&target, 0, sizeof(target));
    target.hz = 94400000.0;
    target.claim = SURVEY_CLAIM_NEW;
    target.verdict = SURVEY_VERDICT_CONFIRMED;
    target.prominence_db = 33.0f;
    target.hits = 6;
    target.looks = 6;
    target.kind_measured = 1;
    target.carrier.found = 1;
    target.carrier.carrier_over_noise_db = 44.2;
    target.carrier.carrier_power_fraction = 0.923;
    target.envelope.found = 1;
    target.envelope.variation = 0.195;
    target.bursts.verdict = SIGNAL_BURST_LEVEL;
    return target;
}

static struct survey_record_setup the_setup(void) {
    struct survey_record_setup setup;

    memset(&setup, 0, sizeof(setup));
    snprintf(setup.receiver, sizeof(setup.receiver), "77771111153705700");
    snprintf(setup.antenna, sizeof(setup.antenna), "a \"quoted\" whip");
    snprintf(setup.site, sizeof(setup.site), "home-desk");
    setup.gain_tenths = 297;
    return setup;
}

/* The whole claim, in one function: everything the JSON writer reads out of
   the application is in the record, and no application was built. */
static void test_a_survey_without_an_application(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct survey_record_setup setup = the_setup();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target target = the_target();

    the_candidates(c);
    check_int("a survey forms from plain facts",
              survey_record_form(&record, &plan, 0.12, &setup, &when, c, 2,
                                 &carrier, 1, &target, 1), 0);

    /* The sweep. */
    check_close("the range it covered, lower", record.plan.lower_hz, 88e6, 1.0);
    check_close("the range it covered, upper", record.plan.upper_hz, 108e6,
                1.0);
    check_int("how many tunings", record.plan.step_count, 13);
    check_int("how many bins", record.plan.bins, 8192);
    check_close("how wide a bin", record.plan.bin_hz, 2441.4, 0.01);

    /*
     * The dwell is the session's and not the plan's, and this fixture makes
     * them differ on purpose: `plan.dwell_seconds` is zero here and the file
     * has always carried 0.12. A record that read the plan's would change the
     * output while every other field agreed, which is the kind of difference
     * a byte-identical comparison is supposed to catch and a summary is not.
     */
    check_close("the dwell it ran, not the one it was planned with",
                record.dwell_seconds, 0.12, 1e-9);
    check_close("and the plan's own dwell is untouched beside it",
                record.plan.dwell_seconds, 0.0, 1e-9);

    /* The receiving setup: ADR-0018 and ADR-0022's three names, and a gain. */
    check_str("which receiver", record.setup.receiver, "77771111153705700");
    check_str("which antenna", record.setup.antenna, "a \"quoted\" whip");
    check_str("which site", record.setup.site, "home-desk");
    check_int("and the gain it was heard at", record.setup.gain_tenths, 297);
    check_int("when it was taken", record.recorded_at.tm_hour, 22);

    /* What it found. */
    check_int("both maxima", record.candidate_count, 2);
    check_int("the carrier they belong to", record.carrier_count, 1);
    check_int("and what was asked again", record.target_count, 1);
    check_close("the first maximum's frequency", record.candidates[0].found_hz,
                94492310.0, 1.0);
    check_str("its allocation, still the band plan's own string",
              record.candidates[0].allocation, "FM broadcast");
    check_true("and the second one's flag survived",
               (record.candidates[1].suspect & SURVEY_SUSPECT_REFERENCE) != 0);
}

/*
 * The decision that used to live inside a printf loop.
 *
 * A candidate takes the verdict of the carrier holding it. Both maxima here
 * sit inside one carrier that a pass confirmed, and only one of them is at
 * the frequency the pass asked about -- so asking at each maximum's own
 * frequency would confirm the first and leave the second unconfirmed, which
 * is what a station's shoulders would all read as.
 */
static void test_a_candidate_takes_its_carriers_verdict(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target target = the_target();

    the_candidates(c);
    /* Put the second maximum inside the carrier too, where a shoulder is. */
    c[1].found_hz = 94480000.0;
    c[1].allocation = "FM broadcast";
    check_int("it forms", survey_record_form(&record, &plan, 0.12, NULL, &when,
                                             c, 2, &carrier, 1, &target, 1), 0);
    check_int("the maximum the pass asked at is confirmed",
              record.candidate_verdict[0], SURVEY_VERDICT_CONFIRMED);
    check_int("and so is the shoulder beside it, because the carrier was",
              record.candidate_verdict[1], SURVEY_VERDICT_CONFIRMED);
    check_int("the carrier itself says the same", record.carrier_verdict[0],
              SURVEY_VERDICT_CONFIRMED);
}

/* And a maximum no carrier holds is asked about at its own frequency, which
   is what leaves a lone peak answerable at all. */
static void test_a_lone_peak_is_asked_about_where_it_is(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target target = the_target();

    the_candidates(c);
    /* The pass asked about the lone peak rather than about the carrier. */
    target.hz = 100000000.0;
    check_int("it forms", survey_record_form(&record, &plan, 0.12, NULL, &when,
                                             c, 2, &carrier, 1, &target, 1), 0);
    check_int("the peak outside every carrier answers for itself",
              record.candidate_verdict[1], SURVEY_VERDICT_CONFIRMED);
    /* PENDING is what `survey_verdict_name()` prints as "unconfirmed":
       nobody asked, which is not the same as having asked and been told no. */
    check_int("and the one inside a carrier nobody asked about does not",
              record.candidate_verdict[0], SURVEY_VERDICT_PENDING);
    check_str("which is written down as unconfirmed",
              survey_verdict_name(record.candidate_verdict[0]),
              "unconfirmed");
}

/*
 * The kind is an index, so a copied record still points inside itself.
 *
 * `survey_confirm_kind_at()` returns a pointer into the array it was handed.
 * Storing that pointer would leave a record whose kind pointed at the
 * caller's stack the moment the record outlived the arrays it was formed
 * from -- which is exactly what a record is for.
 */
static void test_the_kind_survives_being_copied(void) {
    struct survey_record record, copy;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target target = the_target();
    const struct survey_confirm_target *kind;

    the_candidates(c);
    check_int("it forms", survey_record_form(&record, &plan, 0.12, NULL, &when,
                                             c, 2, &carrier, 1, &target, 1), 0);
    kind = survey_record_carrier_kind(&record, 0);
    check_true("the carrier's kind was measured", kind != NULL);
    if (kind)
        check_close("and reads what the pass measured",
                    kind->carrier.carrier_over_noise_db, 44.2, 0.01);

    copy = record;
    memset(&record, 0, sizeof(record));
    kind = survey_record_carrier_kind(&copy, 0);
    check_true("the copy still has one", kind != NULL);
    if (kind) {
        check_true("and it points inside the copy, not at what it was formed"
                   " from", kind >= copy.targets &&
                   kind < copy.targets + SURVEY_CONFIRM_MAX);
        check_close("with the same numbers",
                    kind->carrier.carrier_power_fraction, 0.923, 1e-6);
    }
}

/* A carrier nothing measured has no kind, and says so rather than reading
   zero -- a standing fraction of 0.000 is "heavily modulated" and a burst
   count of zero is "continuous", so zero is the most misleading answer here
   each of these can give. */
static void test_an_unmeasured_carrier_has_no_kind(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();

    the_candidates(c);
    check_int("it forms with nobody having asked",
              survey_record_form(&record, &plan, 0.12, NULL, &when, c, 2,
                                 &carrier, 1, NULL, 0), 0);
    check_true("and the carrier has no kind",
               survey_record_carrier_kind(&record, 0) == NULL);
    check_int("nor a verdict", record.carrier_verdict[0],
              SURVEY_VERDICT_PENDING);
    check_int("an empty pass is none asked", record.target_count, 0);
}

/* The totals both adapters print, counted once. */
static void test_the_totals_are_derived_here(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target targets[3];

    the_candidates(c);
    targets[0] = the_target();
    targets[1] = the_target();
    targets[1].hz = 96000000.0;
    targets[1].verdict = SURVEY_VERDICT_INTERMITTENT;
    targets[2] = the_target();
    targets[2].hz = 97000000.0;
    targets[2].verdict = SURVEY_VERDICT_REFUTED;

    check_int("it forms", survey_record_form(&record, &plan, 0.12, NULL, &when,
                                             c, 2, &carrier, 1, targets, 3), 0);
    check_int("one of the two maxima looks like the receiver",
              record.suspicious, 1);
    check_int("one held up", record.confirmed, 1);
    check_int("one came and went", record.intermittent, 1);
    check_int("one did not hold up", record.refuted, 1);
    check_close("and the tolerance every match was made with is half a bin",
                record.match_hz, 2441.4 / 2.0, 0.01);
}

/*
 * Nothing in the record can change underneath a reader.
 *
 * The setup is the case that matters: the operator can retype a site or an
 * antenna while a save is in flight, and a record holding pointers would then
 * name a place the sweep was not taken at. So the strings are copied, and
 * this mutates the source afterwards to prove it.
 */
static void test_the_record_is_immutable(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct survey_record_setup setup = the_setup();
    struct tm when = a_time();
    struct survey_candidate c[2];
    struct survey_carrier carrier = the_carrier();
    struct survey_confirm_target target = the_target();

    the_candidates(c);
    check_int("it forms", survey_record_form(&record, &plan, 0.12, &setup,
                                             &when, c, 2, &carrier, 1,
                                             &target, 1), 0);
    snprintf(setup.site, sizeof(setup.site), "somewhere-else");
    snprintf(setup.antenna, sizeof(setup.antenna), "a rooftop");
    setup.gain_tenths = 0;
    plan.lower_hz = 0.0;
    c[0].found_hz = 0.0;
    carrier.centre_hz = 0.0;
    target.verdict = SURVEY_VERDICT_REFUTED;

    check_str("the site it was taken at", record.setup.site, "home-desk");
    check_str("the antenna it was heard on", record.setup.antenna,
              "a \"quoted\" whip");
    check_int("the gain it was heard at", record.setup.gain_tenths, 297);
    check_close("the range it covered", record.plan.lower_hz, 88e6, 1.0);
    check_close("where the maximum was", record.candidates[0].found_hz,
                94492310.0, 1.0);
    check_close("where the carrier was", record.carriers[0].centre_hz,
                94400000.0, 1.0);
    check_int("and what the pass concluded", record.targets[0].verdict,
              SURVEY_VERDICT_CONFIRMED);
}

/* What is missing is written as missing, not as an empty string: a reader has
   to be able to tell "nobody said" from "somebody said nothing", because two
   sweeps labelled with an empty site would compare as the same place. */
static void test_nobody_said_is_not_somebody_saying_nothing(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    struct survey_candidate c[2];

    the_candidates(c);
    check_int("a sweep with no setup at all still forms",
              survey_record_form(&record, &plan, 0.12, NULL, &when, c, 2,
                                 NULL, 0, NULL, 0), 0);
    check_str("no receiver identified itself", record.setup.receiver, "");
    check_str("nobody named a site", record.setup.site, "");
    check_int("and no gain was known", record.setup.gain_tenths, 0);
}

/* More maxima than a record holds are truncated rather than refused: they
   have already been truncated once by the peak finder's own bound, and losing
   the whole survey as well is the worse answer. */
static void test_more_than_it_holds(void) {
    static struct survey_candidate many[SURVEY_RECORD_CANDIDATE_MAX + 8];
    static struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();
    int i;

    for (i = 0; i < SURVEY_RECORD_CANDIDATE_MAX + 8; i++) {
        memset(&many[i], 0, sizeof(many[i]));
        many[i].found_hz = 88e6 + (double)i * 1000.0;
    }
    check_int("it forms",
              survey_record_form(&record, &plan, 0.12, NULL, &when, many,
                                 SURVEY_RECORD_CANDIDATE_MAX + 8, NULL, 0,
                                 NULL, 0), 0);
    check_int("and holds what it can", record.candidate_count,
              SURVEY_RECORD_CANDIDATE_MAX);
    check_close("the last one it kept", record.candidates[
                    SURVEY_RECORD_CANDIDATE_MAX - 1].found_hz,
                88e6 + (double)(SURVEY_RECORD_CANDIDATE_MAX - 1) * 1000.0, 1.0);
}

/* And what it refuses. The clock is the adapter's, so a record formed without
   one is not a record formed at some default time. */
static void test_what_it_refuses(void) {
    struct survey_record record;
    struct survey_plan plan = a_plan();
    struct tm when = a_time();

    check_int("no record to fill in",
              survey_record_form(NULL, &plan, 0.12, NULL, &when, NULL, 0, NULL,
                                 0, NULL, 0), -1);
    check_int("no sweep to describe",
              survey_record_form(&record, NULL, 0.12, NULL, &when, NULL, 0,
                                 NULL, 0, NULL, 0), -1);
    check_int("and no time it was taken at",
              survey_record_form(&record, &plan, 0.12, NULL, NULL, NULL, 0,
                                 NULL, 0, NULL, 0), -1);
    check_true("a carrier outside the record has no kind",
               survey_record_carrier_kind(NULL, 0) == NULL);
}

int main(void) {
    test_a_survey_without_an_application();
    test_a_candidate_takes_its_carriers_verdict();
    test_a_lone_peak_is_asked_about_where_it_is();
    test_the_kind_survives_being_copied();
    test_an_unmeasured_carrier_has_no_kind();
    test_the_totals_are_derived_here();
    test_the_record_is_immutable();
    test_nobody_said_is_not_somebody_saying_nothing();
    test_more_than_it_holds();
    test_what_it_refuses();
    return check_report("a finished survey, formed with no application");
}
