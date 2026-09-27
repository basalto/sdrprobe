#include "check.h"

#include "app.h"
#include "fm_view_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * The FM view's model (ticket 07's next view after the survey, ticket 14's
 * Phase 4): the three panels' fields, and the one decision that used to be
 * taken inside `draw_funnel_panel()`.
 *
 * Most of this file is the funnel's closing sentence, and deliberately so.
 * Copying a field out of the view's state fails loudly the moment it is wrong;
 * choosing between five sentences from five counts fails *quietly*, by
 * reporting the wrong diagnosis about a working receiver -- which is the
 * whole reason that choice does not belong in a drawing (ADR-0012).
 *
 * The input is a `struct fm_view`, not a `struct app`. The builder used to
 * take the whole application and reach for one member, so this suite zeroed
 * nine megabytes to fill a handful of fields, and compiled against raylib to
 * do it (`.scratch/layer-boundaries/issues/03-*`). Still a `static`: a
 * `struct fm_view` carries an audio ring and two spectra and has no business
 * on a stack.
 */
static void zero_fm(struct fm_view *fm) {
    memset(fm, 0, sizeof(*fm));
}

/*
 * A pilot that `fm_pilot_locked()` accepts: every one of its five clauses
 * satisfied, at a frequency of exactly 19 kHz.
 *
 * Built by setting the loop's own settled state rather than by feeding it a
 * synthetic tone, because what is under test here is which fields the view
 * model copies and when -- not the loop, which `check-fm-dsp` already owns.
 */
static void lock_pilot(struct fm_view *fm, double hz) {
    struct fm_pilot *p = &fm->session.front.pilot;

    p->sample_rate = 2000000.0;
    p->settled = (long)(FM_PILOT_SETTLE_SECONDS * p->sample_rate) + 1;
    p->incoherent = 1.0;
    p->floor_estimate = 1.0;
    p->amplitude = FM_PILOT_MIN_PRESENCE * 2.0;
    p->coherence = 0.95;
    p->frequency_average = 2.0 * M_PI * hz / p->sample_rate;
}

/*
 * The header restates `struct rds_station`'s two string sizes rather than
 * deriving them, so that a wire writer reading this view model needs nothing
 * from the decoder's own types. This is what keeps that restatement from
 * quietly becoming a truncation -- a name or a radio text one byte short
 * would cut a station's last character and read as a decode fault.
 */
static void test_the_string_sizes_match_the_decoder(void) {
    struct fm_view_model out;
    struct rds_station station;

    check_size("ps is the decoder's own size", sizeof(out.ps),
               sizeof(station.ps));
    check_size("rt is the decoder's own size", sizeof(out.rt),
               sizeof(station.rt));
}

/*
 * The five clauses of the funnel's sentence, each reached only once the one
 * above it is satisfied. The order is the diagnosis: these tests arrange for
 * *later* clauses to be true as well, so a reordering that happens to pass
 * one of them cannot pass all five.
 */
static void test_no_pilot_outranks_every_later_clause(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    /* Blocks, groups and a confirmed name -- everything the three clauses
       below ask about -- and still no pilot to have produced any of it. */
    fm.session.station.funnel.blocks_matched = 400;
    fm.session.station.funnel.groups = 90;
    fm.session.station.ps_valid = 1;

    fm_view_model_build(&fm, &out);

    check_int("no pilot, whatever else arrived", out.pilot_locked, 0);
    check_str("the sentence says so", out.reading,
              "no pilot: not an FM station, or not tuned to one");
    check_int("and reads as a stop", (long)out.reading_tone,
              (long)FM_READING_WEAK);
}

static void test_a_pilot_but_no_blocks_is_a_station_without_rds(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    lock_pilot(&fm, 19000.0);
    fm.session.station.funnel.bits = 5000;   /* offered, none matched */

    fm_view_model_build(&fm, &out);

    check_int("the pilot locked", out.pilot_locked, 1);
    check_str("no blocks is not a fault", out.reading,
              "a pilot but no blocks: this station carries no RDS");
    /* Weak rather than neutral: this is a definite answer about the station,
       not a decode still in progress. */
    check_int("and it is where the decode stopped", (long)out.reading_tone,
              (long)FM_READING_WEAK);
}

static void test_blocks_without_groups_is_weak_reception(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    lock_pilot(&fm, 19000.0);
    fm.session.station.funnel.blocks_matched = 40;

    fm_view_model_build(&fm, &out);

    check_str("blocks but no groups", out.reading,
              "blocks but no groups: too weak to hold sync");
    check_int("reception failing, not the station", (long)out.reading_tone,
              (long)FM_READING_WEAK);
}

static void test_groups_without_a_name_is_still_in_progress(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    lock_pilot(&fm, 19000.0);
    fm.session.station.funnel.blocks_matched = 400;
    fm.session.station.funnel.groups = 90;
    fm.session.station.ps_valid = 0;

    fm_view_model_build(&fm, &out);

    check_str("the name needs all four segments", out.reading,
              "groups arriving; the name needs all four segments");
    /* The one clause that is neither good nor a stop: nothing has gone
       wrong, and nothing is finished. */
    check_int("nothing is wrong yet", (long)out.reading_tone,
              (long)FM_READING_NEUTRAL);
}

static void test_a_named_station_reads_as_working(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    lock_pilot(&fm, 19000.0);
    fm.session.station.funnel.blocks_matched = 400;
    fm.session.station.funnel.groups = 90;
    fm.session.station.ps_valid = 1;

    fm_view_model_build(&fm, &out);

    check_str("reading the station", out.reading, "reading the station");
    check_int("and it reads as working", (long)out.reading_tone,
              (long)FM_READING_GOOD);
}

/*
 * `fm_pilot_hz()` answers whatever the loop last settled on, lock or no lock
 * -- an unlocked loop is tracking a scrap of noise near 19 kHz and its
 * frequency is a fact about that scrap. The window only draws those rows
 * when locked; this model only fills them when locked, which is the same
 * rule in the one place both readers take it from.
 */
static void test_the_pilot_rows_are_empty_without_a_lock(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    /* A loop tracking something, with none of the lock clauses satisfied. */
    fm.session.front.pilot.sample_rate = 2000000.0;
    fm.session.front.pilot.frequency_average =
        2.0 * M_PI * 18950.0 / 2000000.0;
    fm.session.front.pilot.coherence = 0.74; /* the "clean signal, no
                                                    pilot" reading fm_dsp.c
                                                    names outright */
    fm.session.timing_offset = 7;
    fm.session.axis_radians = 1.25;

    fm_view_model_build(&fm, &out);

    check_int("not locked", out.pilot_locked, 0);
    check_close("no frequency is reported", out.pilot_hz, 0.0, 0.0);
    check_close("no offset is reported", out.pilot_ppm, 0.0, 0.0);
    check_int("no symbol timing", out.timing_offset, 0);
    check_close("no subcarrier axis", out.axis_radians, 0.0, 0.0);
    /* Coherence is the exception, and on purpose: it is measured with or
       without a lock, and it is what says how close the loop came. */
    check_close("the coherence is still reported", out.pilot_coherence, 0.74,
                1e-9);
}

static void test_the_pilot_rows_are_filled_when_locked(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    /* 19 kHz + 19 Hz is exactly +1000 ppm, which no real transmitter is;
       chosen so a wrong denominator in the ppm arithmetic cannot pass. */
    lock_pilot(&fm, 19019.0);
    fm.session.timing_offset = 7;
    fm.session.axis_radians = 1.25;

    fm_view_model_build(&fm, &out);

    check_int("locked", out.pilot_locked, 1);
    check_close("the pilot's own frequency", out.pilot_hz, 19019.0, 1e-6);
    check_close("as an offset from 19 kHz", out.pilot_ppm, 1000.0, 1e-6);
    check_int("the symbol timing offset", out.timing_offset, 7);
    check_int("out of a symbol's samples", out.timing_samples_per_symbol,
              FM_RDS_SAMPLES_PER_SYMBOL);
    check_close("the subcarrier axis", out.axis_radians, 1.25, 1e-9);
}

static void test_the_station_panel_fields(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.session.station.pi_valid = 1;
    fm.session.station.pi = 0x8343;
    fm.session.station.pi_repeats = 12;
    fm.session.station.ps_valid = 1;
    snprintf(fm.session.station.ps, sizeof(fm.session.station.ps),
             "%s", "TSF");
    fm.session.station.rt_valid = 1;
    snprintf(fm.session.station.rt, sizeof(fm.session.station.rt),
             "%s", "Cultura em antena2.rtp.pt");

    fm_view_model_build(&fm, &out);

    check_int("the identification", (long)out.pi, 0x8343);
    check_int("how many groups agreed", out.pi_repeats, 12);
    check_str("the name", out.ps, "TSF");
    check_str("the radio text, unwrapped", out.rt,
              "Cultura em antena2.rtp.pt");
}

/*
 * The panel says "%d of 4 segments", so the count is what both readers show
 * and the mask is the decoder's own bookkeeping. Three bits of the four,
 * deliberately not the low three, so a model returning the mask itself (11)
 * or its lowest set bit cannot pass.
 */
static void test_ps_segments_is_a_count_not_a_mask(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.session.station.ps_segments = 0x0B; /* segments 0, 1 and 3 */

    fm_view_model_build(&fm, &out);

    check_int("three of the four have arrived", out.ps_segments, 3);
}

/*
 * A programme type of 0 is a legal value ("no programme type"), so `pty` on
 * its own cannot say whether one has been received -- which is why
 * `pty_valid` exists and why the name and the traffic line are left empty
 * without it, rather than filled from index 0.
 */
static void test_the_programme_type_waits_for_its_valid_flag(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.session.station.pty = 3;
    fm.session.station.tp = 1;
    fm.session.station.ta = 1;

    fm_view_model_build(&fm, &out);

    check_int("nothing has been received", out.pty_valid, 0);
    check_int("so the type is not reported", out.pty, 0);
    check_str("nor its name", out.pty_name, "");
    check_str("nor the traffic line", out.traffic, "");

    fm.session.station.pty_valid = 1;
    fm_view_model_build(&fm, &out);

    check_int("and once it has", out.pty_valid, 1);
    check_int("the type is carried", out.pty, 3);
    check_str("named the way the window names it", out.pty_name,
              rds_pty_name(3));
    check_str("and so is the traffic line", out.traffic,
              rds_traffic_name(1, 1));
}

/*
 * The sound is this machine's business, not the station's: a remote reader
 * cannot hear it. `broadcast_stereo` is a fact about the transmission and is
 * carried either way; the rate is only meaningful while a device is open.
 */
static void test_the_audio_rate_waits_for_something_playing(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.audio.stereo = 1;
    fm.audio.audio_rate = 50000.0;

    fm_view_model_build(&fm, &out);

    check_int("the station transmits stereo", out.broadcast_stereo, 1);
    check_int("nothing is playing", out.playing, 0);
    check_close("so no rate is reported", out.audio_rate_hz, 0.0, 0.0);

    fm.playing = 1;
    fm_view_model_build(&fm, &out);

    check_int("playing", out.playing, 1);
    check_close("and the rate is the device's", out.audio_rate_hz, 50000.0,
                1e-9);
}

static void test_the_audio_error_is_carried_verbatim(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    snprintf(fm.audio_error, sizeof(fm.audio_error), "%s",
             "no audio device");

    fm_view_model_build(&fm, &out);

    check_str("the reason, as written", out.audio_error, "no audio device");
}

static void test_the_multiplex_spectrum_is_copied(void) {
    static struct fm_view fm;
    struct fm_view_model out;
    int i;

    zero_fm(&fm);
    fm.spectrum_bins = 256;
    fm.spectrum_bin_hz = 125.0;
    for (i = 0; i < 256; i++)
        fm.spectrum[i] = (float)(-100 + i);

    fm_view_model_build(&fm, &out);

    check_int("as many bins as were measured", out.spectrum_bins, 256);
    check_close("and the bin width beside them", out.spectrum_bin_hz, 125.0,
                1e-9);
    check_close("the first bin", (double)out.spectrum[0], -100.0, 1e-6);
    check_close("the last", (double)out.spectrum[255], 155.0, 1e-6);
    check_close("and nothing past it", (double)out.spectrum[256], 0.0, 0.0);
}

/*
 * Nothing has been measured before the first refresh, which is a fact rather
 * than a placeholder -- an empty chart is the correct drawing of it, and a
 * bin count of zero is what says so.
 */
static void test_no_spectrum_before_the_first_refresh(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm_view_model_build(&fm, &out);

    check_int("no bins", out.spectrum_bins, 0);
    check_close("and no bin width to go with them", out.spectrum_bin_hz, 0.0,
                0.0);
}

/*
 * The cap is the array's own length, so nothing a real refresh produces is
 * ever truncated -- but a reader sizing a wire buffer from this header is
 * entitled to rely on it, and a `spectrum_bins` past the end would be a
 * buffer overrun in whoever believed it.
 */
static void test_the_bin_count_is_clamped_to_the_array(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.spectrum_bins = FM_VIEW_MODEL_MAX_BINS + 500;

    fm_view_model_build(&fm, &out);

    check_int("clamped to what the array holds", out.spectrum_bins,
              FM_VIEW_MODEL_MAX_BINS);
}

static void test_the_funnel_counts_are_carried(void) {
    static struct fm_view fm;
    struct fm_view_model out;

    zero_fm(&fm);
    fm.session.station.funnel.bits = 12000;
    fm.session.station.funnel.blocks_matched = 430;
    fm.session.station.funnel.groups = 98;
    fm.session.station.funnel.identified = 95;
    fm.session.station.funnel.named = 4;

    fm_view_model_build(&fm, &out);

    check_int("soft bits", out.bits, 12000);
    check_int("blocks", out.blocks_matched, 430);
    check_int("groups", out.groups, 98);
    check_int("identified", out.identified, 95);
    check_int("named", out.named, 4);
}

int main(void) {
    test_the_string_sizes_match_the_decoder();
    test_no_pilot_outranks_every_later_clause();
    test_a_pilot_but_no_blocks_is_a_station_without_rds();
    test_blocks_without_groups_is_weak_reception();
    test_groups_without_a_name_is_still_in_progress();
    test_a_named_station_reads_as_working();
    test_the_pilot_rows_are_empty_without_a_lock();
    test_the_pilot_rows_are_filled_when_locked();
    test_the_station_panel_fields();
    test_ps_segments_is_a_count_not_a_mask();
    test_the_programme_type_waits_for_its_valid_flag();
    test_the_audio_rate_waits_for_something_playing();
    test_the_audio_error_is_carried_verbatim();
    test_the_multiplex_spectrum_is_copied();
    test_no_spectrum_before_the_first_refresh();
    test_the_bin_count_is_clamped_to_the_array();
    test_the_funnel_counts_are_carried();
    return check_report("the FM view's model: its three panels, and the "
                        "funnel's own sentence");
}
