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

/*
 * What the receiver is currently doing. One owner: whoever passes it in.
 *
 * `generation` is ADR-0027's tuning generation: a plain counter advanced by
 * the transactions below, on success, and by nothing else. It exists so a
 * consumer downstream of this struct -- a Viewer -- can tell that a
 * measurement in flight belongs to the tuning before this one and decline to
 * draw it under the new frequency.
 *
 * **It is advanced here rather than by the caller, and that is the point.**
 * It used to be `retune_receiver()`'s own `generation++` in `sdrprobe.c`,
 * which left the Settings panel -- a second stop/apply/flush/read-back/restart
 * transaction of its own -- moving the receiver while the generation said
 * nothing had moved. Two writers assigned different meanings to one value
 * that ADR-0027 had already made external. A caller cannot advance it
 * correctly because only the transaction knows which of its steps took: the
 * identity and its generation move together or neither moves.
 */
struct receiver_applied {
    uint32_t frequency_hz;
    uint32_t sample_rate_hz;
    int ppm;
    uint32_t generation;
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
 * What a gain change asks for, and what the gain was before it.
 *
 * Gain is a parameter here and **not a field of `struct receiver_applied`**,
 * which is the same refusal as the one above: an AD9361's receive gain is a
 * table index and a tuner's is a step in tenths of a decibel
 * (`GAIN_UNIT_INDEX`, `device_profile.h`), so which fields an applied-gain
 * struct should hold is a question for the second receiver rather than one to
 * answer now. The caller keeps `applied_manual_gain` / `applied_gain_tenths`
 * and assigns them once this returns 0. What the transaction needs is only
 * what to set and what to put back.
 */
struct receiver_gain {
    int manual;
    int tenths;
};

/*
 * Tune, leaving the rate alone. Returns 0, or -1 with a reason in `error`.
 *
 * The steps are stop, correction, frequency, flush, read back, start -- and
 * every failure after the first puts the previous frequency and correction
 * back, flushes, and restarts. The read-back is a step of its own because a
 * device that cannot say where it is tuned has not been tuned as far as
 * anything downstream is concerned.
 *
 * A success advances `applied->generation`; every failure leaves it, along
 * with the frequency and the correction it belongs to.
 */
int receiver_runtime_tune(struct receiver_runtime *rt, uint32_t frequency_hz,
                          int ppm);

/*
 * The same transaction with a gain change at the front of it: stop, gain,
 * correction, frequency, flush, read back, start. `had` is what the gain was,
 * and every failure after the gain was written puts it back with the tuning.
 *
 * One transaction and not two, deliberately. The Settings panel used to run
 * its own copy of this sequence, and doing the gain in a separate transaction
 * ahead of a tune would let the gain take while the tuning rolled back --
 * leaving the receiver at a sensitivity nobody asked for, with a refusal on
 * screen saying nothing happened.
 *
 * `receiver_runtime_tune()` is this with the gain left untouched -- which is
 * not the same as asking for the gain it already has. `device_set_gain()`
 * carries a manual/automatic flag that nothing can read back, so a retune
 * that restated the current gain would switch a manual receiver to automatic
 * every time anything tuned it.
 */
int receiver_runtime_apply(struct receiver_runtime *rt, uint32_t frequency_hz,
                           int ppm, struct receiver_gain want,
                           struct receiver_gain had);

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
