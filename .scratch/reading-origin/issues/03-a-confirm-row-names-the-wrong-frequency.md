# 03 - A confirm row prints one frequency and another frequency's flags

Status: needs-triage
Opened 2026-09-11, from a live sweep during `.scratch/device-model/issues/11-*`.

A headless confirmation row prints the **target's** frequency and the
suspicion flags computed from the **measured centre**, and those are not the
same number. From a 128-137 MHz sweep on 2026-09-11:

```
confirm 136079346 new confirmed 22.0 6/6 3906 reference,unresolved,clocked-here
```

136.079346 is 79 kHz from 1.6 x 85, which is three times
`RECEIVER_COMB_TOLERANCE_HZ` and about eighty times the tolerance
`clocked-here` is allowed. Neither flag can be true of the frequency printed
beside them. Both are true of the centre the pass measured, near 136.004,
which is where `survey_session_confirm_finish()` reads
`s->confirm.best.centre_hz` and the row does not.

## It is pre-existing, and it is not new

`reference` has had this property since the confirmation pass existed. What
changed is that `clocked-here` makes it *visible*: a comb flag 79 kHz from a
multiple looks like a loose tolerance, where a coherence flag 79 kHz from
exact looks like what it is, which is a flag about a different frequency.
That is worth saying because it is an argument for the new flag rather than
against it -- a tighter measurement found a fault the loose one hid.

## Why this is triage and not ready

**Which number the row should carry is a real question, not a typo.** Three
answers and they are not equivalent:

- **Print the measured centre.** Truthful about the flags, and it silently
  changes what a `confirm` row's first field means -- from "what was asked
  about" to "what was found", which is a file-format change under ADR-0016 and
  breaks matching against the `candidate` rows above it, which carry the
  frequency the sweep found.
- **Print both**, as `probe-signal` was made to do for exactly this class of
  confusion: the offset it was asked for and the offset it found. Widest row,
  and the most honest.
- **Compute the flags at the asked-for frequency.** Wrong: the pass measured
  where the signal actually is, and re-deriving flags at a frequency the
  signal is not at throws away the better measurement.

**And how far apart they are allowed to be before it is a different signal.**
79 kHz is a long way. `survey_session_measure()` retunes to the candidate and
the carrier search has a window; a centre that lands 79 kHz off may be the
same carrier measured properly, or it may be the search having found a
stronger neighbour -- which is the fault `carrier_75000_bare.bin`'s note
warns about, where a window across the whole span returns a real neighbour at
+176 kHz. Nothing in the row says which, and the answer decides whether this
is a reporting fix or a measurement one.

## What must be checkable

That a row's flags are flags *of* the frequency the row names, whichever of
the three answers is chosen -- which no check asserts today, for either the
new flags or `reference`. `check-survey-session` drives the machine with a
fake block and is where the pair would be pinned.
