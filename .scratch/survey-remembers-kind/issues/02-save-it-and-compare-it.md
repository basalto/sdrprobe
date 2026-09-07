# 02 - Save what kind of thing it was, and compare two sweeps by it

Status: resolved

Ticket 01 measures the kind and prints it. A saved survey still records
frequency, level, width, shape, suspicion and confirmation, and none of it --
so the finding lives in a terminal and is lost on disk, which is the opposite
of what `surveys/` is for.

## Where it goes in the file

On the **carrier**, not on a confirmation target. Carriers are the unit `diff`
compares and the unit the history remembers; a target is a thing the pass
happened to ask about. `survey_confirm_for()` already matches a target to a
frequency and the saved file already uses it to put `"confirmed"` on each
carrier -- the kind goes the same way.

Both writers have to do it. `survey_store.c` writes the file when the window
or `--survey-save` does, and `scripts/survey_tool.py ingest` writes it from a
script's stdout, and the two already differ: the C side records confirmation
as four counts and no per-target detail, the Python side keeps a `targets`
array. Putting the kind on the carrier is the shape both can produce.

## Two things already dropped, worth picking up in the same pass

The `confirm` line carries a width and suspicion flags that the pass measured
at 244 Hz where the sweep could only see 212 kHz bins, and
`survey_tool.py parse()` reads only the first six fields. Both are already
being thrown away.

## What report and diff should say

`report`: a bare carrier is worth calling out on its own. "Strong, confirmed,
continuous, and nothing riding it" is the answer that saves somebody an
afternoon, and it is what 75.000 MHz turned out to be.

`diff`: **a change of kind is the finding this effort exists for.** A signal
whose standing fraction moves from 0.87 to 0.003 has changed in a way no level
and no width would show, and a carrier verdict that flips is worth printing
above anything about decibels. A change of 8 dB already gets a line; this
should get a louder one.

## What must not happen

A verdict in the history. `site_history` records what was heard, not what it
measured to. "Bare in June, modulated in September" is a comparison a reader
makes from two files, and the program's job is to have kept both numbers --
the same line `signal_findings.h` holds.

## What must be checkable

The C writer's output shape, in `check-survey-store`, which already pins the
rest of the file. `survey_tool.py` has no check suite and this ticket does not
add one; what it can do is keep the parser tolerant of a file without the
field, since every survey recorded before today is one.

## Comments

**2026-09-07 — done.** Both writers record the kind on each carrier, the
parser reads it back, `report` calls out carriers with nothing riding them and
`diff` reports a change of kind on its own line.

```
the pass measured what kind of thing 2 of them were, and 1 carries nothing
      74.996 MHz   -42.4 dBFS  bare carrier, 93% of the channel standing still  Aeronautical radionavigation
```

Under a band plan reading "Aeronautical radionavigation", which is the case
`.scratch/signal-probe/` was raised for and the first time it survives the
window closing.

### Two bugs found by running it rather than by reading it

**The JSON was malformed.** The `kind` object was emitted after
`"allocation": ` had already been written, so every saved file with a kind in
it was unparseable. `check-survey-store` passed throughout, because no fixture
carried one -- a green tick over a branch nothing exercised. The fixture now
carries a real one, taken from a real pass, and the check asserts the whole
object including the field that follows it.

**The carrier search was aimed at the wrong place.** A target is aimed at its
carrier's `centre_hz`, the middle of the extent, and `survey_carrier.h` warns
outright that this "parts company" with where the energy sits. On one sweep of
74-76 MHz the middle landed 42 kHz from the 75.000 MHz line and the search
measured the noise beside it:

| aimed at | verdict | over floor | standing |
| --- | --- | --- | --- |
| the middle of the extent | a modulated carrier | 15.4 dB | 1% |
| where the energy is | **a bare carrier** | **44.2 dB** | **92%** |

Widening the search does not fix it -- the extent is 2.9 kHz, so there is
nothing in the width to widen by. `power_centre_hz` goes on the target and the
search aims there, and two consecutive sweeps whose grouping put the centre
14 kHz apart both read a bare carrier at 92-95%.

### One trap re-entered and closed properly

Aiming at the energy meant the measurement needed the target, and the first
version reached for it through `confirm.index` -- which the headless pass
never advances, so every headless target's measurement would have landed in
the first one, while looking right on screen. `survey_confirm_look()` takes
the target itself now, so the shape cannot come back.

### Picked up in passing

`survey_tool.py parse()` read only the first six fields of a `confirm` line
and dropped the width and the suspicion flags -- the only honest measurement
of either, taken at 244 Hz where the sweep could see 212 kHz bins.

### Still divergent, and not this ticket

The two writers disagree about the rest of the file. The C side puts
`"confirmed"` on every carrier and records confirmation as four counts; the
Python side keeps a `targets` array and no per-carrier `confirmed`. That
predates this work, and a reader diffing a C-written file against a
Python-written one sees fields appear and vanish.
