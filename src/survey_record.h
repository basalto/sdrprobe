#ifndef SURVEY_RECORD_H
#define SURVEY_RECORD_H

#include <time.h>

#include "installation.h"
#include "survey_carrier.h"
#include "survey_confirm.h"
#include "sdr_dsp.h"
#include "reading_origin.h"
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

/*
 * What one maximum of a sweep turned out to be.
 *
 * `struct survey_candidate` exists so that what a candidate *is* -- where it
 * was found, what it measured to, whether it resembles the receiver, which
 * allocation it falls in -- is decided in one place and then printed, or
 * written, or drawn. It used to be decided inside a printf loop, which meant
 * the window and the headless report could disagree about the same peak
 * without anybody noticing.
 */

struct survey_candidate {
    double found_hz;          /* where the sweep found it */
    double centre_hz;         /* what measuring refined it to; 0 if unmeasured */
    double width_hz;          /* occupied bandwidth; 0 if unmeasured */
    /*
     * How wide it is in the survey array, from the peak's own -20 dB extent.
     * Coarser than `width_hz` and always available, which is the point: a
     * swept survey has no spectrum to measure a candidate out of, and without
     * this it could say nothing at all about the shape of what it found. Big
     * and meaningless when the extent walk hit its bound, which happens to a
     * candidate with no -20 dB point of its own -- that reads as "not narrow",
     * which is the safe way for it to be wrong.
     */
    double extent_hz;
    float power_dbfs;
    float prominence_db;
    int measured;
    unsigned int suspect;     /* SURVEY_SUSPECT_* */
    const char *allocation;   /* band plan name, or NULL */
};

/* The suspicion flags as the text both outputs use: "reference,step-centre",
   or "-" for none. Returns `buffer`, or a literal for none. */
/*
 * How much room `survey_flag_text()` needs for every flag at once.
 *
 * Four call sites each wrote `char flags[64]`, which was ample for four flags
 * and eight bytes short of six. That is the ordinary way a magic number
 * expires: nothing referred to the set it was sized for, so adding to the set
 * did not touch it. The formatter also clamps now -- it accumulated
 * `snprintf()`'s *would-have-been* length, so one truncation made `size -
 * used` wrap to an enormous size_t and the next write ran off the end.
 */
#define SURVEY_FLAG_TEXT_MAX 96

const char *survey_flag_text(unsigned int flags, char *buffer, size_t size);

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
 * What the receiver was doing while the sweep ran.
 *
 * These facts are all `survey_candidates_from()` ever wanted out of
 * `struct app`, and taking them explicitly is what lets a candidate's meaning
 * be decided without an application: where the receiver was pointed and how
 * fast it was sampling, so a bin index becomes a frequency; the reference
 * clock, so `survey_suspect()` can ask whether a maximum looks like the
 * receiver's own comb -- **0 when the source has no crystal to blame, which is
 * a capture's case, and then nothing may be attributed to one**; whether the
 * spectrum had DC removed, because the bin at zero is the receiver's own
 * offset when it did not; and what the crystal is still doing.
 */
struct survey_record_tuning {
    double centre_hz;
    double sample_rate_hz;
    double reference_clock_hz;
    int remove_dc;
    /*
     * This receiver's own reference error, and the correction in force.
     *
     * **A crystal error of 0 is a refusal and not a good receiver**: it is
     * what an uncalibrated receiver gets, because nobody has measured its
     * error, and what a capture gets, because a file does not carry its
     * recorder's crystal. A *calibrated* receiver is emphatically not in that
     * case -- the two hypotheses simply swap which of them reads on the
     * nominal (`.scratch/device-model/issues/11-*`).
     */
    struct reading_clock clock;
};

/*
 * Everything one finished survey is made of, as facts rather than as an
 * application.
 *
 * `spectrum` is non-NULL only when the whole survey came from one tuning and
 * every candidate can be measured out of it; across a swept range it belongs
 * to whichever step happened to be last, and a bandwidth read from it would be
 * a number about the wrong signal. That is `survey_session_spectrum()`'s own
 * refusal and both adapters have always honoured it -- this makes it an
 * argument rather than a convention.
 *
 * `scratch` is `SAMPLE_BLOCK_PAIRS` floats for the carrier measurement to sort
 * in, or NULL to measure nothing. It is the caller's because it is large and
 * because the record must not own a block-sized buffer to hold a page of
 * results.
 */
struct survey_record_input {
    struct survey_record_tuning tuning;
    const struct survey_plan *plan;
    double dwell_seconds;
    struct survey_record_setup setup;
    struct tm recorded_at;
    const struct sdr_peak *peaks;
    int peak_count;
    const float *spectrum;
    float *scratch;
    const struct survey_carrier *carriers;
    int carrier_count;
    const struct survey_confirm_target *targets;
    int target_count;
};

/*
 * The receiving setup, out of the installation that knows it.
 *
 * Both adapters need the same four values and neither should be spelling
 * `installation.receiver` into a record field itself: ADR-0018 and ADR-0022
 * put the identity in one place, and this is the one read of it. A receiver
 * with no identity leaves `receiver` empty rather than inventing a label.
 */
void survey_record_setup_from(struct survey_record_setup *out,
                              const struct installation *inst,
                              int gain_tenths);

/*
 * The local time now, for an adapter to stamp a record with.
 *
 * The clock is deliberately the adapter's rather than the record's: a record
 * formed twice from one sweep would otherwise be two different records, and
 * the file's `recorded_at` and its filename would each be whenever their own
 * line ran.
 */
struct tm survey_record_now(void);

/*
 * Work out what each maximum is, and form the whole record from it. Returns 0,
 * or -1 if anything needed is missing.
 *
 * This is the one entry an adapter needs: the window's save and the headless
 * report both call it, so what a candidate *is* -- where it was found, what it
 * measured to, whether it resembles the receiver, which allocation it falls in
 * -- is decided once for both. It used to be decided inside a `printf` loop.
 */
int survey_record_build(struct survey_record *out,
                        const struct survey_record_input *in);

/*
 * The candidates alone, for a caller that has its own reason to want them.
 * `survey_record_build()` is this followed by `survey_record_form()`; both
 * halves are separate so the materialisation can be checked without forming a
 * record around it. Returns how many were filled in.
 */
int survey_record_candidates(const struct survey_record_tuning *tuning,
                             const struct survey_plan *plan,
                             const struct sdr_peak *peaks, int count,
                             const float *spectrum, float *scratch,
                             struct survey_candidate *out, int max);

/*
 * Form the record from candidates already worked out. Returns 0, or -1 if
 * anything needed is missing.
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
