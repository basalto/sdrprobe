# 01 — Tell a burst from a mistake: a third verdict

Status: needs-triage

The confirmation pass has two answers and needs three. "It was there", "it was
not", and "it was there some of the time" are different findings, and the third
is the correct description of every mobile-satellite candidate this receiver
sees.

## Where to start

In `survey_confirm_step()` and `survey_confirm_sweep()`, which already take
`SURVEY_CONFIRM_LOOKS` looks at each target and already have somewhere to put
the count: `struct survey_measurement` and `survey_measure_observe()` in
`src/survey_sweep.h` were built for exactly this and are wired to nothing but
the survey view's two-second measurement.

The pass peak-holds its looks into one spectrum (which is right -- it is what
lets it see a burst at all) and then asks once. Asking per look as well as
holding across them costs nothing extra: the blocks are already being folded,
and `sdr_dsp_characterise_carrier()` on each of six is well under a
millisecond.

So each target gains a duty: hits over looks. Then

- **6/6** is continuous and confirms as it does today;
- **1/6 to 5/6** is a burst, and the honest word is `intermittent` or `bursty`,
  which `survey_measure_duty_label()` already spells;
- **0/6** is refuted, and only that is refuted.

## What must be checkable

- The verdict for each duty, as a pure function, the way
  `survey_confirm_verdict()` already is -- and the same care about the sense of
  it, since a "missing" claim inverts the whole thing and a third value doubles
  the ways that can go wrong.
- That a bursty verdict is recorded in the site history rather than discarded.
  `survey_confirm_should_record()` is where that is decided and it currently
  has two cases; a signal heard in two of six looks is heard.
- The saved JSON and the `confirm` line carry the duty, not just the word. The
  format already has `confirmed`/`refuted`/`unconfirmed` per candidate
  (`docs/band-surveys.md`); a fourth value needs the same treatment and the
  reader in `scripts/survey_tool.py`.

## What this must not become

A reason to confirm everything. One look in six is also what a noise excursion
that happened once looks like, and the point of the pass is that it refutes
those. The distinction has to come from the count being *repeatable* -- a
carrier heard in two of six looks and again in two of the next six is bursty; a
single hit that never recurs is not. `SURVEY_CONFIRM_LOOKS` is six today and
that may be too few to tell those apart; measuring where it stops being able to
is part of the ticket, not an afterthought (`does-it-help`).
