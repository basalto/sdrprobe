#ifndef RECEIVER_LEASE_H
#define RECEIVER_LEASE_H

/*
 * Who borrowed the receiver's tuning, and what to put back when they give it
 * up.
 *
 * Nine screens used to keep their own `return_frequency` and restore it
 * themselves. Each was individually right and no rule connected them, so
 * nothing said that overlapping owners give the receiver back in the reverse
 * order they took it. The overlaps are real -- LTE opens calibration, GSM
 * starts a band scan, the survey asks its candidates again, and the automatic
 * drift check interrupts whatever is on screen -- and an out-of-order return
 * strands the operator at a frequency nobody chose.
 *
 * A strict LIFO stack of prior tuning snapshots is that rule, and this header
 * is the whole of it: plain integers, no receiver, no window, so the ordering
 * can be checked without either (ADR-0012).
 *
 * What it is not:
 *
 * - It does not own hardware. A snapshot is what the receiver *was* set to;
 *   putting it back is the caller's business, and `retune_receiver()` is
 *   still the only thing that touches a dongle.
 * - It does not duplicate the current tuning. `app->applied_frequency` and
 *   `app->applied_sample_rate` remain the sole truth for that.
 * - It does not carry PPM. Applying a calibration is deliberate persistent
 *   state and must survive every return; rolling one back with the tuning is
 *   the bug this refuses to make possible. There is no field to get wrong.
 */

#include <stdint.h>

/* Two owners is the deepest any current path goes -- LTE then calibration,
   GSM then band scan, survey then confirmation. Four leaves room for the
   third level the checks exercise and for a fourth nobody has built, and
   refuses beyond that rather than growing: a stack that deepens without
   bound is a leak that looks like nesting. */
#define RECEIVER_LEASE_MAX 4

/* What the receiver was set to. Both fields, always: a frequency-only owner
   records the rate it did not change, so returning restores a pair rather
   than half of one. */
struct receiver_tuning {
    uint32_t center_hz;
    uint32_t sample_rate_hz;
};

/*
 * An owner's claim on the stack.
 *
 * The generation is what makes a stale token safe. An index alone would let a
 * token returned twice name whatever later owner happens to be at that depth,
 * which is precisely the failure this replaces -- silent, and indistinguishable
 * from a correct return until the operator notices the wrong frequency.
 * Generations never repeat within a run, so a second return names nothing.
 *
 * Zero means inactive, so a zeroed struct is an owner holding nothing and no
 * constructor is needed.
 */
struct receiver_lease_token {
    uint32_t generation;
};

struct receiver_lease {
    struct receiver_tuning saved[RECEIVER_LEASE_MAX];
    uint32_t generation[RECEIVER_LEASE_MAX];
    int depth;
    uint32_t next_generation;
};

static inline void receiver_lease_reset(struct receiver_lease *lease) {
    lease->depth = 0;
    lease->next_generation = 1;
}

static inline int receiver_lease_depth(const struct receiver_lease *lease) {
    return lease->depth;
}

static inline int receiver_lease_token_active(
        const struct receiver_lease_token *token) {
    return token != 0 && token->generation != 0;
}

/*
 * Take the receiver, recording where it was found.
 *
 * Returns 0 and fills `token`, or negative when the stack is full -- in which
 * case the token is left inactive and the caller has borrowed nothing. A
 * refusal here is a bug in the caller's nesting, not a condition to handle.
 */
static inline int receiver_lease_acquire(struct receiver_lease *lease,
                                         struct receiver_tuning current,
                                         struct receiver_lease_token *token) {
    if (lease->depth >= RECEIVER_LEASE_MAX) {
        token->generation = 0;
        return -1;
    }
    if (lease->next_generation == 0)
        lease->next_generation = 1;
    lease->saved[lease->depth] = current;
    lease->generation[lease->depth] = lease->next_generation;
    token->generation = lease->next_generation;
    lease->next_generation++;
    lease->depth++;
    return 0;
}

/* Whether `token` is the newest active claim. Every operation below needs
   this, and an out-of-order return is exactly this returning false. */
static inline int receiver_lease_is_top(const struct receiver_lease *lease,
                                        const struct receiver_lease_token *token) {
    return lease->depth > 0 && token->generation != 0 &&
           lease->generation[lease->depth - 1] == token->generation;
}

/*
 * Undo an acquisition that never took effect.
 *
 * The apply failed, `retune_receiver()` has already put the hardware back, and
 * the snapshot on the stack now describes a borrowing that never happened.
 * Only the top may be cancelled, for the same reason only the top may be
 * returned.
 */
static inline int receiver_lease_cancel(struct receiver_lease *lease,
                                        struct receiver_lease_token *token) {
    if (!receiver_lease_is_top(lease, token))
        return -1;
    lease->depth--;
    token->generation = 0;
    return 0;
}

/*
 * Look at what this owner has to put back, without giving it up.
 *
 * Returning is two-phase because the restore can fail: the receiver can refuse
 * a frequency it accepted a minute ago. Popping first would lose the snapshot
 * and leave nothing to retry with, so the pop waits for
 * `receiver_lease_finish_return()` and a failed restore leaves the owner
 * holding a token that still works.
 *
 * The lease is `const` here, so a one-phase return is a compile error rather
 * than a check failure: this cannot be made to pop no matter who calls it.
 */
static inline int receiver_lease_begin_return(const struct receiver_lease *lease,
                                              const struct receiver_lease_token *token,
                                              struct receiver_tuning *out) {
    if (!receiver_lease_is_top(lease, token))
        return -1;
    *out = lease->saved[lease->depth - 1];
    return 0;
}

/* The restore succeeded. Give the claim up. */
static inline int receiver_lease_finish_return(struct receiver_lease *lease,
                                               struct receiver_lease_token *token) {
    if (!receiver_lease_is_top(lease, token))
        return -1;
    lease->depth--;
    token->generation = 0;
    return 0;
}

/*
 * Give the claim up without restoring anything.
 *
 * The survey's "Open waterfall" is what this is for: a candidate was picked,
 * the receiver has been tuned to it deliberately, and putting back the tuning
 * the sweep started from is the opposite of what was just asked for. Written
 * as a commit rather than as a quietly cleared flag, because "keep this" and
 * "I forgot to put it back" should not look the same in the source.
 */
static inline int receiver_lease_commit(struct receiver_lease *lease,
                                        struct receiver_lease_token *token) {
    if (!receiver_lease_is_top(lease, token))
        return -1;
    lease->depth--;
    token->generation = 0;
    return 0;
}

#endif /* RECEIVER_LEASE_H */
