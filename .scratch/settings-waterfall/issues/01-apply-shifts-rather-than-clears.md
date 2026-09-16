# 01 - Apply shifts the waterfall rather than clearing it

Status: needs-triage

## Description

`apply_settings()` ends with `recreate_waterfall(app, app->plot, 1)` -- the 1
is `clear_history` -- so pressing Apply empties the waterfall whatever changed.
Every other retune in the program shifts the history instead
(`sdr_dsp_retune_bin_shift()`, `view_scope.c`), which is a decision this
repository has already measured once: on an FM scan retuning every half second
the history carried **7 rows before the shift existed and 14 after**, and half
the picture was lost to a single arrow press.

The panel no longer moves the frequency -- the Scope header does, and this
re-applies `app->applied.frequency_hz` -- so in the common case the shift is
zero rows and the correct behaviour is to keep the history untouched. What can
still move it is a PPM change, which moves the effective tuning slightly, and
a gain change, which moves nothing in frequency but does move every level in
the history.

## What makes this more than tidying

Three of the four things this panel applies have different right answers, and
one clear treats them alike:

- **PPM**: the tuning moves by `centre * delta_ppm / 1e6` -- 3.4 kHz for one
  ppm at 3.4 GHz, 100 Hz at 100 MHz. Sub-bin at most settings. Shift.
- **Gain**: the frequency axis does not move at all, so the history stays
  aligned -- but every row below the change was measured at a different gain,
  and a waterfall whose colours mean two different things is worse than one
  that is short. This is the case that may genuinely want a clear, or a mark.
- **Remove DC**: changes one bin.
- **Scope resolution**: changes the bin count, which is a different chart --
  `process_block()` already drops the rows on a geometry change, so this one
  is handled and must not be handled twice.

So the answer is probably not "shift instead of clear" but "do what the change
actually did to the picture", and gain is the interesting one.

## Tasks

- [ ] Decide the per-change answer above, gain first. Say what a row measured
      at another gain should look like rather than assuming it must go.
- [ ] Use the existing shift; do not add a second implementation of it.
- [ ] Leave the resolution case to `process_block()`'s geometry drop.
- [ ] Check it where the arithmetic is, not by eye: the shift is already
      checkable and a mutation of its direction is already pinned.
- [ ] `make screens NAMES="scope"` before and after, with an Apply in between.

## Comments

**2026-09-16** -- Found while routing `apply_settings()` through
`receiver_runtime_apply()` (commit `bab6319`), and deliberately left alone
there: that change was about one writer for the receiver transaction, and this
is a drawing decision. It did move out of the transaction, though -- the clear
used to sit inside it and roll the receiver back if the allocation failed,
which stopped being expressible once the transaction owned the tuning
generation, since rolling back afterwards would leave the generation advanced
with the frequency put back.
