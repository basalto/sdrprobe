# 12 - Three faults the Viewer link had no check for

Status: needs-triage

## Why this is a ticket and not a guideline

Ticket 07 introduced three faults, found all three by driving the thing live,
and fixed all three before committing. None shipped. The question this ticket
exists to answer is the other one: **what would have caught them without a
person watching a live sweep for 75 seconds.**

The tempting answer is a note somewhere saying "remember not to do this".
This repository has already run that experiment and written down the result.
`CLAUDE.md`'s two audits -- `NOT GATED` and `MISSING` -- exist because
*"the failure is invisible from a green run"*, and ADR-0012 is the general
form: every decision the program makes must be reachable by a check that
needs no window, no receiver and no person.

**One of these three is decisive on the point.** The publish spin is the
*same bug* ticket 10 fixed six days ago, in the same file, reintroduced by
the person who had just read ticket 10 while writing the code that
reintroduced it. Ticket 10's fix even produced a named, checked predicate --
`viewer_update_due()`, covered by `check-viewer-session` -- and the new code
did not use it; it wrote a bare inline `if` instead. A guideline is weaker
than a fix that was read, understood, and still not applied. So: checks.

Where a guideline *is* the right instrument, it is documentation at the point
of use -- a comment where the mistake gets made -- and one is proposed below.

## 1. Two clocks with different origins

`viewer_session_handle_command()` computed `now` as a raw
`monotonic_seconds()` -- absolute host uptime, tens of thousands of seconds --
while `viewer_session_run()`'s loop computes it relative to a `started`
baseline. `survey_start()` stamped `step_started_at` from the first; every
later tick measured against the second. `now - step_started_at` was deeply
negative and stayed negative.

The mechanism is one line, and it is in a header everything survey reads:

```c
static inline enum survey_step_phase
survey_step_phase_at(double elapsed, double dwell_seconds, ...) {
    if (elapsed < SURVEY_SETTLE_SECONDS)
        return SURVEY_STEP_SETTLING;
```

A negative `elapsed` is less than the settle, so the sweep reads "the tuner
has not caught up" **forever**. One retune in 75 seconds where thirteen were
due. `survey_measure_settled()` has the identical shape and the identical
hole.

**A negative elapsed is not a small number: it is a different clock.** A
monotonic clock does not run backwards, so the value cannot arise from timing
-- only from two origins being compared. Reading it as "still settling" is the
worst available response, because settling is silent and looks like patience.

Proposed, `-lm` alone, `check-survey-sweep`:

- `survey_step_phase_at()` and `survey_measure_settled()` given a negative
  elapsed do **not** report settling. Whether that is a refusal, a clamp or an
  assertion is the decision this ticket owes -- a clamp masks the fault, so
  prefer whichever is loudest that a header of `static inline` functions can
  do.
- The fix already in place -- one file-static origin both the handler and the
  loop read -- stays. This is the second line of defence, for the next caller
  that acquires a clock of its own.

## 2. The publish spin, twice

Publishing `survey_spectrum`/`survey_state` on every loop iteration measured
**234216 messages in 10 seconds**. `viewer_link_publish_*()` queues into a
replaceable slot rather than sending, so an unconditional republish leaves
`select()` permanently ready and the loop never waits. Gating both on
`spectrum_updated`, as `spectrum` and `waterfall_row` already were, brought it
to 151 in 10 seconds (~15.1/s, the block rate).

Ticket 10 is the same finding about `receiver_state`. Its fix named the
decision -- `viewer_update_due()` -- and `check-viewer-session` covers it. The
new streams did not join it.

Proposed:

- **The gate becomes a predicate with a name**, the way ticket 10's did, and
  every stream's publish decision goes through one. ADR-0012's own rule for
  this is already written: a function that runs the loop may not also decide.
  Today `viewer_session_run()` decides inline for four streams and calls
  `viewer_update_due()` for two.
- `check-viewer-session` then enumerates it: for each of
  `VIEWER_STREAM_COUNT`, publishing is due only when its source changed.
  That check fails the day a seventh stream is added ungated, which is the
  property wanted -- it is the enumeration the inline `if` cannot have.
- `make bench-serve` (added 2026-09-16) is the live confirmation and should be
  named in the acceptance criteria of any ticket adding a stream. It is what
  turns "seems fine" into a number.

## 3. Two string tables with no enumeration behind them

**The subscribe parser.** `handle_subscribe_line()` is a hand-matched chain,
one `else if` per name, with nothing to audit it against. It had no branch for
`survey_spectrum` or `survey_state`, so a client subscribing to them got
nothing -- no refusal, no error, an empty stream. Silent by construction: the
parser's contract is that an unrecognised token is ignored.

**The name table.** `stream_names[VIEWER_STREAM_COUNT]` is sized by the enum,
so adding two enum values left the last two entries **NULL** (C zero-fills a
short initializer) rather than out of bounds -- passed to `%s`, which is
undefined behaviour that glibc happens to render as `(null)`.

**The buffer under it.** The subscription summary in the debug log was
`char summary[64]`, sized when there were five names. Two more truncated it
mid-word: `subscribed: spectrum waterfall receiver_state link_health
survey_spectrum s`. A log line that lies about what a client asked for, in
the one line that exists to answer that question. Now 160 with the reasoning
written beside it.

All three are the same shape: a list that must stay in step with an enum,
with nothing checking that it does. This is `CLAUDE.md`'s `NOT GATED` audit
one level down.

Proposed:

- Expose the table -- `viewer_link_stream_name(enum viewer_stream)` -- so a
  check can reach it at all. Today it is `static` in the `.c`.
- `check-viewer-link` asserts every enum value has a non-NULL, non-empty,
  unique name, and that the summary buffer holds all of them space-separated
  with room to spare.
- **A round-trip for the parser**, which is the half that matters and is
  already cheap: `check-viewer-link` drives *real loopback sockets* with a
  from-scratch client. For every subscribable name: connect, subscribe,
  assert the stream arrives. `command_result` is deliberately not
  subscribable -- a command's answer goes to whoever sent it -- so the check
  pins that exemption by name rather than looping blindly over the enum.

## Proposed guideline -- one, at the point of use

Not a skill and not a section in `CLAUDE.md`, both of which are read before
the work rather than during it. A comment in `src/viewer_link.h`, above the
publish functions, saying what a publish *is*: a queue into a replaceable
slot, not a send; that an ungated republish therefore spins the serve loop;
that this has now been measured twice at four and five orders of magnitude
over the block rate; and that `make bench-serve` is how to tell. Whoever adds
the next stream reads that file to find the function they are about to call.

## Acceptance criteria

- [ ] A negative elapsed does not read as settling, and a check says so.
- [ ] Adding a stream without gating its publish fails `check-viewer-session`.
- [ ] Adding a stream without a name, or without a subscribe token, fails
      `check-viewer-link`.
- [ ] `src/viewer_link.h` says what a publish costs, where the call is made.
- [ ] No new `check-*` rule is left out of `CHECK_UNITS`, and no new header
      out of `APP_HDR` -- both audits in `CLAUDE.md`, both one line.

## Not in scope

- The browser page's own verification: ticket 11.
- Ticket 09's subscription-driven computation. Gating a *publish* on fresh
  data is not the same question as not computing what nobody asked for.
