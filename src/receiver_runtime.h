#ifndef RECEIVER_RUNTIME_H
#define RECEIVER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "device_backend.h"

/*
 * Changing what the receiver is doing, as a transaction that either takes or
 * leaves it where it was found.
 *
 * `retune_receiver()` and `retune_receiver_at_rate()` in `sdrprobe.c` are a
 * multi-step stop/apply/flush/restart with a rollback at every step, and
 * **not one of those steps is reachable by a check**: they take `struct app`,
 * they live in the translation unit with `main()`, and the receiver path is
 * the half no check reaches anyway (ADR-0012). The sequence has been correct
 * by inspection for as long as it has existed.
 *
 * So the sequence lives here, over a device session and a lifecycle it is
 * handed, and `check-receiver-runtime` drives it against a fake of each.
 *
 * **It owns no state, deliberately.** The applied settings and the error
 * buffer are the caller's and are pointed at rather than copied.
 * `.scratch/deepening/issues/10-*` is explicit about why: an applied-state
 * struct here beside the one in `struct app` would be two owners of one fact
 * before either has one writer, and which fields that struct should hold is a
 * question for the second receiver. This is the transaction; ticket 10's
 * later phases move the state in behind it.
 */

/* What the receiver is currently doing. One owner: whoever passes it in. */
struct receiver_applied {
    uint32_t frequency_hz;
    uint32_t sample_rate_hz;
    int ppm;
};

/*
 * Stopping and starting the sample worker.
 *
 * A seam rather than a call, because the transaction has to stop and restart
 * acquisition at four points and the worker's lifecycle still lives in
 * `sdrprobe.c` with the thread, the signal mask and the choice of worker
 * function. There are two implementations from the first day -- the real one
 * and the check's -- so this is not a vtable with a single filler.
 *
 * Both return 0 or -1. A stop with nothing running must return **0**: the
 * RTL-SDR backend's own `stop()` returns -1 in that case (measured, phase 1a),
 * and a transaction that read that as a failure would refuse to retune a
 * receiver that was merely idle.
 */
struct receiver_lifecycle {
    int (*stop)(void *ctx);
    int (*start)(void *ctx);
    void *ctx;
};

struct receiver_runtime {
    struct device_session *source;      /* borrowed */
    struct receiver_applied *applied;   /* borrowed: one owner, the caller's */
    char *error;                        /* borrowed */
    size_t error_size;
    struct receiver_lifecycle life;
    /*
     * Whether this is a receiver at all. A capture holds one frequency at one
     * rate with one gain baked in, so every transition below refuses rather
     * than pretending -- and says so in a sentence, because "nothing
     * happened" is the answer a reader cannot act on.
     */
    int live;
};

/*
 * Tune, leaving the rate alone. Returns 0, or -1 with a reason in `error`.
 *
 * The steps are stop, correction, frequency, flush, read back, start -- and
 * every failure after the first puts the previous frequency and correction
 * back, flushes, and restarts. The read-back is a step of its own because a
 * device that cannot say where it is tuned has not been tuned as far as
 * anything downstream is concerned.
 */
int receiver_runtime_tune(struct receiver_runtime *rt, uint32_t frequency_hz,
                          int ppm);

/*
 * Tune and change the rate together. Returns 0, or -1 with a reason.
 *
 * Two transactions nested, and the order matters: the rate is applied first
 * because every spectrum and every decoder's expectation depends on it, and
 * **if the tuning then fails the rate is put back too**, so a refusal leaves
 * the receiver exactly where it was found. That is the case
 * `check-receiver-runtime` exists for, and the one no check could reach
 * before: rate succeeds, frequency fails, and what the caller is left holding
 * has to be the old rate, the old tuning, one running worker and a sentence
 * naming what refused.
 *
 * An unchanged rate delegates to `receiver_runtime_tune()` rather than
 * stopping the worker to set the rate it already has.
 */
int receiver_runtime_tune_at_rate(struct receiver_runtime *rt,
                                  uint32_t frequency_hz,
                                  uint32_t sample_rate_hz, int ppm);

/* Set the correction only if it differs, so an unchanged ppm costs nothing
   and cannot fail. Shared with `sdrprobe.c`, which had it first. */
int receiver_runtime_set_correction(struct device_session *source, int ppm);

#endif
