#ifndef SURVEY_STORE_H
#define SURVEY_STORE_H

#include <stddef.h>
#include <time.h>

#include "sdr_dsp.h"
#include "survey_carrier.h"
#include "survey_sweep.h"

/*
 * A finished sweep, written down so the next one can be compared with it.
 *
 * The survey window measures a band and then loses it: the peaks live until
 * the next sweep and the reader either wrote them down or did not. That is
 * fine for looking and useless for noticing, and noticing is the point --
 * whether a carrier appeared, whether one went, whether the band is as it was
 * last month.
 *
 * So a sweep can be saved. The file is JSON under `surveys/`, the same shape
 * `scripts/survey_tool.py` produces from the headless path, so the two are
 * interchangeable and one reporting tool reads both.
 *
 * `struct survey_candidate` exists so that what a candidate *is* -- where it
 * was found, what it measured to, whether it resembles the receiver, which
 * allocation it falls in -- is decided in one place and then printed, or
 * written, or drawn. It used to be decided inside a printf loop, which meant
 * the window and the headless report could disagree about the same peak
 * without anybody noticing.
 */

struct app;

struct survey_candidate {
    double found_hz;          /* where the sweep found it */
    double centre_hz;         /* what measuring refined it to; 0 if unmeasured */
    double width_hz;          /* occupied bandwidth; 0 if unmeasured */
    float power_dbfs;
    float prominence_db;
    int measured;
    unsigned int suspect;     /* SURVEY_SUSPECT_* */
    const char *allocation;   /* band plan name, or NULL */
};

/* The suspicion flags as the text both outputs use: "reference,step-centre",
   or "-" for none. Returns `buffer`, or a literal for none. */
const char *survey_flag_text(unsigned int flags, char *buffer, size_t size);

/*
 * Work out the facts about each peak. `spectrum` is non-NULL only when the
 * whole survey came from one tuning and every candidate can be measured out of
 * it; across a swept range it belongs to whichever step was last, and a
 * bandwidth read from it would be a number about the wrong signal.
 *
 * Returns how many were filled in.
 */
int survey_candidates_from(struct app *app, const struct survey_plan *plan,
                           const struct sdr_peak *peaks, int count,
                           const float *spectrum,
                           struct survey_candidate *out, int max);

/*
 * The two fiddly parts, kept pure so a check can reach them (ADR-0012).
 */

/*
 * `2026-09-02-185703-24M-1766M.json`, matching what the ingest script names its
 * files. Returns the length, or -1 if it did not fit.
 *
 * The time is in it because the date was not enough. Four sweeps of the whole
 * tuner in one day left one file: each overwrote the last, silently, including
 * one that had a note attached. A directory whose purpose is that sweeps
 * accumulate cannot lose them for being taken on the same afternoon.
 */
int survey_store_filename(double lower_hz, double upper_hz,
                          const struct tm *when, char *out, size_t size);

/* A JSON string body, with the handful of characters that must be escaped
   escaped. Returns the length, or -1 if it did not fit. An antenna is named by
   a person and there is nothing stopping them using a quote or a backslash. */
int survey_json_escape(const char *in, char *out, size_t size);

/*
 * Write the sweep to `surveys/`, creating the directory if need be. The path
 * written is copied into `path_out`. Returns 0, or -1 with a reason on stderr.
 */
int survey_store_write(const struct app *app, const struct survey_plan *plan,
                       const struct survey_candidate *candidates, int count,
                       const struct survey_carrier *carriers, int carrier_count,
                       char *path_out, size_t path_size);

#endif
