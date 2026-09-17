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

## Implementation notes -- where each piece goes

Written out because this ticket will be picked up cold, and because two of
the three touch files whose checks already exist and only need extending.

**1. The negative elapsed.** Both functions are `static inline` in
`src/survey_sweep.h` -- `survey_step_phase_at()` (line ~323) and
`survey_measure_settled()` (~335). Check goes in `tests/survey_sweep_test.c`
(`make check-survey-sweep`), which already links `-lm` alone and needs no new
fixture: these take doubles and return an enum.

The open decision, and it is the whole of the work: **what a negative elapsed
should do.** Three candidates, none obviously right --

- *Clamp to zero* -- simplest, and wrong: it turns the fault into a sweep that
  merely starts its settle late, which is silent, which is how this one
  survived 75 seconds.
- *Return a new `SURVEY_STEP_IMPOSSIBLE`* -- honest, but every caller then has
  a case to handle and most would handle it by ignoring the block, which is
  `SETTLING` again by another name.
- *Refuse loudly* -- `debug_log_write()` is not reachable from a header of
  `static inline` functions with no state, and an `assert()` in a shipping
  path is a decision this repository has not made anywhere else.

Prefer whichever makes the *caller* say what it saw.
`survey_session.c:521` is the one call site (`now - s->step_started_at`) and it
has a session, a log and somewhere to put an error string -- so the likeliest
answer is that the header exposes the predicate (`survey_elapsed_sane()`) and
the *session* refuses, which keeps the header pure and puts the noise where
something can hear it. Decide it in the ticket, not in the commit.

**2. The publish predicate.** `viewer_session.c` already has the pattern to
copy: `viewer_update_due()` (added by ticket 10, covered by
`check-viewer-session`, `tests/viewer_session_test.c` -- today one report
line, *"when a Viewer metadata update is due"*). Four streams decide inline in
`viewer_session_run()` and two go through the predicate; the work is making
that one and enumerating it over `VIEWER_STREAM_COUNT`.

Take into account: the two families are **not** paced the same way and the
predicate must not flatten them. `receiver_state`/`link_health` are paced on
*time* (they change without a block arriving); `spectrum`/`waterfall_row`/
`survey_spectrum`/`survey_state` are gated on *new data* (`spectrum_updated`).
A single "is this due?" that ignores the difference would either spin the
metadata streams or stall the data ones. One predicate, two reasons, both
named.

`make bench-serve` is the live confirmation. Idle `--serve` measured 9.8% CPU
and 98.6% with a metadata subscriber before ticket 10; a stream added ungated
shows up there immediately.

**3. The two tables.** `stream_names[]` is `src/viewer_link.c:76`, declared
`static const char *const stream_names[VIEWER_STREAM_COUNT]` -- sized by the
enum, so a short initializer yields NULL entries rather than an overrun.
Exposing it is one small public function (`viewer_link_stream_name()`),
which is also what lets the debug-log summary and any future client share one
spelling.

`handle_subscribe_line()` is `src/viewer_link.c:267`. `tests/viewer_link_test.c`
(`make check-viewer-link`) already connects over **real loopback sockets** with
a from-scratch masking client, so the round trip is a loop over names, not new
scaffolding.

Take into account: **`VIEWER_STREAM_COMMAND_RESULT` is deliberately not
subscribable** -- a command's answer goes to whoever sent the command -- so a
blind loop over the enum would fail on a correct exemption. Pin it by name,
with the reason, so the next stream that is exempt has to say why.

**Both audits apply to anything added here**, and both are one line in
`CLAUDE.md`: a new `check-*` rule that is not in `CHECK_UNITS` passes, is
picked up by `check-touched`, and is never run by the gate or the pre-push
hook; a new `src/*.h` not in `APP_HDR` means editing it does not rebuild the
binary. Run both after, not before.

## What "done" is not

Not a fourth check that passes today because the bug is fixed. Each of these
has to fail against the **reintroduced** fault -- revert the fix, watch the
check go red, restore it. That is this repository's mutation discipline
(`.claude/skills/check-claims`), and on this ticket it is the whole point:
all three faults are already fixed, so a check written against the fixed code
and never run against the broken code proves nothing at all.
