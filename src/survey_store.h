#ifndef SURVEY_STORE_H
#define SURVEY_STORE_H

#include <stddef.h>
#include <time.h>

#include "survey_record.h"

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
 * What a sweep *found* is `survey_record.h`'s: this module is the JSON
 * adapter over it and decides nothing about the survey itself.
 */

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
 * Write a finished survey to `surveys/`, creating the directory if need be.
 * The path written is copied into `path_out`. Returns 0, or -1 with a reason
 * on stderr.
 *
 * One argument, because this is an adapter now: everything it writes was
 * decided in `survey_record.{c,h}` and this module chooses only how JSON is
 * spelled, what the file is called, and what to do when that name is taken.
 * It used to take the application, a plan and four arrays, and work out the
 * confirmation verdicts for itself in a `printf` loop the headless report
 * could not reach.
 *
 * The time in the file and the time in its name are `record->recorded_at`,
 * so the two say the same instant by construction rather than by both calling
 * the clock and hoping.
 */
int survey_store_write(const struct survey_record *record,
                       char *path_out, size_t path_size);

#endif
