# 01 — Tell a burst from a mistake: a third verdict

Status: resolved

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

## Comments

**2026-09-05 — resolved, with one limit found and left open.**

Each look is now measured on its own block and the hits are counted;
`survey_confirm_presence()` turns the count into a verdict using
`survey_measure_duty_label()`'s own boundary rather than a new constant, so a
duty the rest of the program calls continuous is not called intermittent here.
`intermittent` does not invert between "new" and "missing" — a signal heard in
two looks of six came and went whichever the sweep had said about it, and that
is deliberate, since a third value doubles the ways the inversion can go wrong.

`survey_confirm_should_record()` records it, which is the half that mattered: a
refuted "new" is never written down, so before this the next sweep called it
new again, the next pass refuted it again, and the mobile-satellite
allocations could not enter the history however many sweeps heard them.

Measured on air. The band the ticket is about, three passes over 1600-1670 MHz:

| pass | asked | confirmed | intermittent | refuted |
| --- | --- | --- | --- | --- |
| 1 | 0 | -- | -- | -- |
| 2 | 9 | 1 | 2 (1/6, 1/6) | 6 |
| 3 | 7 | 1 | 4 (1/6, 2/6, 3/6, 3/6) | 2 |

Six signals that were refuted before are now recorded as heard. The controls
hold: band II gives 24 of 24 confirmed at 6/6 every time, so the strict
boundary does not demote a continuous station, and 120.0 MHz -- the continuous
air-band carrier -- reads 6/6 at 42 dB while its neighbours come and go.

**The level is the best single look, not the hold.** Peak-holding was the first
implementation and it is the wrong instrument: holding raises the noise floor
along with the signal, so 129.839 MHz -- present in two looks of six -- came
back at 4.7 dB, under the 6 dB bar it had cleared twice, with the number
contradicting the verdict printed beside it. `struct survey_view.confirm` no
longer carries a spectrum at all.

### The limit, which the ticket half-anticipated

Six consecutive blocks span about 0.4 s, so `intermittent` only sees signals
that switch on a sub-second timescale. That is Iridium, whose bursts are
milliseconds -- which is why the table above works. It is **not** the
phenomenon the spec opened with: candidates that appear in one sweep and not
the next vary over *minutes*, and within any single 0.4 s visit such a signal
is wholly present or wholly absent, so it still reads confirmed or refuted.

Air-band traffic shows it plainly. Transmissions last seconds, so three passes
over 118-137 MHz gave 6/6 and 0/6 and almost nothing between; the one
intermittent reading there, 129.839 MHz at 2/6, was a signal keying inside the
window rather than a talking aircraft.

Telling minute-scale variation from noise needs the pass to *revisit*, not to
look longer in one place -- which is `--survey-watch`'s territory and the
history's, not this pass's. Worth its own ticket if it is worth doing;
`site_history_seen()` already distinguishes on/off from by-hour, and now that
bursts reach the history at all it has something to work with.
