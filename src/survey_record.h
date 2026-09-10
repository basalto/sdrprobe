#ifndef SURVEY_RECORD_H
#define SURVEY_RECORD_H

#include <time.h>

#include "installation.h"
#include "survey_carrier.h"
#include "survey_confirm.h"
#include "survey_store.h"
#include "survey_sweep.h"

/*
 * One finished survey, as a fact rather than as a screen or a file.
 *
 * `survey_session` owns the sweep, the confirmation pass, the watch and the
 * measurement, and both the window and the headless path drive that one
 * machine. What it *found* still had no such owner: `survey_store.c` includes
 * `app.h` and decides three different things at once -- what a candidate
 * means, which receiving setup produced it, and how JSON is spelled -- while
 * `survey_report.c` formats the same finished survey a second time. Two
 * formatters over one answer is the shape the survey machine was extracted to
 * end, and it survived in the output.
 *
 * The measurement that says so is in the store's own check: it allocates a
 * whole `struct app` to write one file. A test that has to build the
 * application to exercise a file writer is telling you the writer's interface
 * asks its callers to know nearly everything.
 *
 * So a record is formed first, from explicit facts, and the adapters format
 * it. The record holds no pointer into the application and nothing that can
 * change under a reader.
 *
 * **What is decided here rather than while printing.** A candidate takes the
 * verdict of the carrier that holds it -- a carrier's shoulders are maxima of
 * the same signal a few bins away, and reading each of those as "never asked"
 * would leave most of a confirmed station's own list unconfirmed. That rule
 * lived inside the JSON writer's loop, where the headless report could not
 * reach it and did not share it. It is `survey_record_form()`'s job now, and
 * an adapter reads the answer.
 *
 * ADR-0015 still holds: the band plan is a lookup and the record does not
 * identify a technology from it.
 */

/* As many maxima as a sweep can raise: the peak finder's own bound, so a
   record can hold whatever a survey produced without a second limit to
   disagree about. At 56 bytes a candidate the whole record is about 38 KB,
   which is a caller's array rather than something to put in a loop. */
#define SURVEY_RECORD_CANDIDATE_MAX SURVEY_MAX_PEAKS

/*
 * The receiving setup that produced a sweep, copied in.
 *
 * ADR-0018 and ADR-0022: a history and a calibration belong to a receiver, a
 * site and an antenna together, so a sweep that cannot say which of each took
 * it cannot be matched to either. Copied rather than pointed at because the
 * operator can retype any of them while a record is being written, and a
 * record that changes underneath its own adapters is not a record.
 *
 * An empty `receiver` means the device offered no identity -- which many of
 * these dongles do not -- and an empty `site` means nobody said. Both are
 * written as null rather than as "" so a reader can tell those apart from
 * somebody having said nothing. `gain_tenths` of 0 or less means the gain was
 * not known, for the same reason: 0 dB is a gain.
 */
struct survey_record_setup {
    char receiver[INSTALLATION_ID_MAX];
    char antenna[INSTALLATION_ID_MAX];
    char site[INSTALLATION_ID_MAX];
    int gain_tenths;
};

struct survey_record {
    struct survey_plan plan;
    /*
     * What the sweep actually dwelled, which is **not** `plan.dwell_seconds`.
     *
     * The plan carries the dwell it was made with and the session carries the
     * one it ran; they agree on any sweep this program plans for itself, and
     * the JSON writer has always taken the session's. Carrying it separately
     * keeps that choice visible instead of leaving a reader to assume the two
     * are one number, which is exactly the assumption that would silently
     * change the file if they ever parted.
     */
    double dwell_seconds;
    struct survey_record_setup setup;
    struct tm recorded_at;

    struct survey_candidate candidates[SURVEY_RECORD_CANDIDATE_MAX];
    int candidate_count;
    struct survey_carrier carriers[SURVEY_CARRIER_MAX];
    int carrier_count;
    struct survey_confirm_target targets[SURVEY_CONFIRM_MAX];
    int target_count;

    /*
     * What was concluded about each of the above, decided once.
     *
     * `candidate_verdict[i]` is `enum survey_verdict` for candidates[i],
     * taken from the carrier holding it; `carrier_verdict[i]` the same for a
     * carrier, asked at its own centre. `carrier_kind[i]` indexes `targets`
     * for the pass that measured what kind of thing that carrier is, or -1
     * when none did -- an index rather than a pointer so the record survives
     * being copied.
     */
    signed char candidate_verdict[SURVEY_RECORD_CANDIDATE_MAX];
    signed char carrier_verdict[SURVEY_CARRIER_MAX];
    int carrier_kind[SURVEY_CARRIER_MAX];

    /*
     * The totals both adapters print. Derived, and here so they are derived
     * once: the headless report and the JSON writer each counted these, from
     * the same arrays, in their own loops.
     */
    int suspicious;
    int confirmed;
    int intermittent;
    int refuted;

    /*
     * How far a reported frequency may sit from the one a pass asked about:
     * half a bin, the quantisation the sweep put on it. Kept because it is
     * the tolerance every match above was made with, and a reader re-deriving
     * it from `plan.bin_hz` would be re-deciding rather than reading.
     */
    double match_hz;
};

/*
 * Form the record. Returns 0, or -1 if anything needed is missing.
 *
 * Counts above the record's bounds are truncated rather than refused: a
 * survey that found more than the peak finder's own limit has already been
 * truncated once, and losing the file as well would be the worse answer.
 * `recorded_at` is the adapter's, because the clock is not a property of what
 * was measured -- a record formed twice from one sweep is otherwise two
 * different records.
 */
int survey_record_form(struct survey_record *out,
                       const struct survey_plan *plan, double dwell_seconds,
                       const struct survey_record_setup *setup,
                       const struct tm *recorded_at,
                       const struct survey_candidate *candidates, int count,
                       const struct survey_carrier *carriers,
                       int carrier_count,
                       const struct survey_confirm_target *targets,
                       int target_count);

/* The target that measured this carrier's kind, or NULL. The index is in the
   record; this is the read, so an adapter never does the -1 test itself. */
const struct survey_confirm_target *
survey_record_carrier_kind(const struct survey_record *record, int carrier);

#endif
