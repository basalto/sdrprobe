#ifndef SURVEY_TUNING_H
#define SURVEY_TUNING_H

#include "core/reading_origin.h"

/*
 * The four facts about a tuning that a candidate has to be read against.
 *
 * Out of `survey_record.h` (`runtime/`) because `survey_view_model` takes one
 * and the models may not reach upward (ADR-0028). It is the same split as
 * `site_seen.h`: the *contract* is a model, and gathering it --
 * `survey_tuning_from()`, which reads `struct app` -- stays in runtime.
 */
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

#endif
