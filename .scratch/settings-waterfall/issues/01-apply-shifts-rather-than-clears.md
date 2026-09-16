# 01 - Apply shifts the waterfall rather than clearing it

Status: resolved, 2026-09-16

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

- [x] Decide the per-change answer above, gain first. Say what a row measured
      at another gain should look like rather than assuming it must go.
- [x] Use the existing shift; do not add a second implementation of it.
- [x] Leave the resolution case to `process_block()`'s geometry drop.
- [x] Check it where the arithmetic is, not by eye.
- [~] `make screens NAMES="scope"` before and after, with an Apply in between.
      Narrowed to `settings`, and to structure rather than an Apply -- see
      below.

## Comments

**2026-09-16** -- Found while routing `apply_settings()` through
`receiver_runtime_apply()` (commit `bab6319`), and deliberately left alone
there: that change was about one writer for the receiver transaction, and this
is a drawing decision. It did move out of the transaction, though -- the clear
used to sit inside it and roll the receiver back if the allocation failed,
which stopped being expressible once the transaction owned the tuning
generation, since rolling back afterwards would leave the generation advanced
with the frequency put back.

## Done, 2026-09-16

The decision turned out narrower than the four-way split above once traced
through the code, and gain really is the only case that needed a rule.

**PPM never reaches the existing shift, because it never needed to.**
`sdr_dsp_retune_bin_shift()` computes its shift from
`app->applied.frequency_hz`, which a PPM-only Apply does not change --
`receiver_runtime_tune()` re-asserts the same frequency it was given, and the
device reports the same number back. The true received frequency does move,
by `centre * delta_ppm / 1e6`, but nothing in the program represents that
number anywhere the waterfall reads, so there was no second shift to write:
the existing per-frame mechanism already answers "nothing moved" for exactly
the case that is sub-bin at every setting this panel accepts. Writing a
PPM-specific shift would have been a second implementation measuring a
quantity the first one is correct to ignore.

**Remove DC and Scope resolution needed no code at all** -- the former
touches one bin, and the latter is `process_block()`'s own geometry drop,
already handled and confirmed untouched.

**Gain is the only case, and it is now one function**:
`sdr_dsp_gain_change_clears_waterfall()` in `sdr_dsp.h`, beside
`sdr_dsp_retune_bin_shift()`. It takes the old and new manual/tenths pair and
answers whether the change should clear -- comparing `manual` as well as
`tenths`, because switching to automatic gain with the same last-known tenths
on record is still a change: automatic is not pinned to that number going
forward. `apply_settings()`'s receiver branch computes it from `had`/`want`
before either is overwritten; the capture branch calls the same function with
`(0, 0, 0, 0)` rather than a bare `0`, so both branches read as answering the
same question instead of one reasoning about it and one asserting it.

**One check, and two mutations run against it**, per the earlier ticket's own
methodology (`check-sdr-dsp`, 204 checks, all passing on the first run):
dropping the `manual` comparison and swapping `||` for `&&` each failed a
different assertion. `make check-touched`: every suite pinned by
`sdr_dsp.h`, `sdr_dsp_test.c` or `overlay_settings.c` passes.

**The image criterion is narrower than asked, for the same reason ticket
`testability/10` found**: there is no way to script pressing Apply here --
clicking cannot be injected any more than a key can, and gain is not even
adjustable under `--file` playback, which is every headless comparison this
program can run. `make screens NAMES="settings"` before and after this
change is structurally identical (same panel, same "capture (not adjustable)"
gain line) -- which confirms the *drawing* is untouched, not that a live
Apply behaves correctly. That second half needs a person with the receiver
attached: open Settings, change gain, Apply, and watch whether the waterfall
clears; then change only PPM, Apply, and watch that it does not.

`make check`: full gate, no failures.
