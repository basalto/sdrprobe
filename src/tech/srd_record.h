#ifndef SRD_RECORD_H
#define SRD_RECORD_H

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

/*
 * The SRD view's record-duration field: what a typed string may mean, before
 * anything touches the receiver or a file.
 *
 * Header-only and pure, the way calibration_gate.h is, so the one thing this
 * view lets an operator type is reachable by a check with no window
 * (ADR-0012) rather than only by clicking Record and reading what happened.
 */

/* Below one sample block (65.5 ms at 2 MS/s) a capture would hold nothing
   worth a decode attempt. */
#define SRD_RECORD_SECONDS_MIN 0.1
/* 30 s at 2 MS/s is about 120 MB; long enough to catch several button
   presses of a slow remote, short enough that "Record" cannot fill a disk by
   mistake. */
#define SRD_RECORD_SECONDS_MAX 30.0

/*
 * Parse a typed record duration in seconds.
 *
 * Empty text (NULL or "") is not a value to refuse -- it is "use the
 * default" -- and yields SRD_RECORD_SECONDS_DEFAULT with a 0 return, the
 * same rule config_set_fft_size()'s callers use for an unset field.
 *
 * Anything else must be a plain decimal number (no unit suffix -- this is
 * seconds, not hertz) inside [SRD_RECORD_SECONDS_MIN, SRD_RECORD_SECONDS_MAX].
 * Out of range or unparseable is a *refusal*: returns -1 and leaves *out
 * untouched, so a caller can print why rather than silently clamping to a
 * duration nobody typed.
 */
#define SRD_RECORD_SECONDS_DEFAULT 2.0

static inline int srd_record_seconds(const char *text, double *out) {
    char *end = NULL;
    double parsed;

    if (!out)
        return -1;
    if (!text || !text[0]) {
        *out = SRD_RECORD_SECONDS_DEFAULT;
        return 0;
    }

    errno = 0;
    parsed = strtod(text, &end);
    if (errno || !end || end == text || *end || !isfinite(parsed))
        return -1;
    if (parsed < SRD_RECORD_SECONDS_MIN || parsed > SRD_RECORD_SECONDS_MAX)
        return -1;

    *out = parsed;
    return 0;
}

#endif /* SRD_RECORD_H */
