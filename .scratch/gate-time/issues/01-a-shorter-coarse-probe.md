# 01 - The coarse scan does not need the full probe

Status: needs-triage

`signal_find_carrier()` costs `O(window / spacing * probe)` with
`spacing = rate / probe * 4`, which is `O(window * probe^2 / rate)`. The probe
is capped at 300 000 pairs, so a full-span search at 2 MS/s is ~75 000 grid
points each mixing 300 000 pairs.

A line's width in a length-N mix is about `rate/N`. So a coarse stage using
32 768 pairs may step at ~244 Hz rather than 26.7, which is nine times fewer
points each nine times cheaper -- about **80x** off the coarse stage -- with
the existing refinement then walking down to the same final resolution, and the
final measurement already taken at full length.

## What a prototype did, and why this is a ticket rather than a patch

Tried: coarse stage at 32 768 pairs, refinement starting at one coarse step and
narrowing as before.

**`check-signal-probe` went from 105 s to 3 s, and five checks failed.** Not
marginal ones -- the standing fraction collapsed, `0.0118` where `0.679` was
expected, which is what an off-frequency mix reads. So the coarse stage landed
on the wrong line in the synthetic cases, or the prototype's refinement window
did not recover the coarse error it now has to recover. Either way the answer
moved, and this function's answers are pinned on a real capture
(`carrier_75000_bare.bin`: +299 478.2 Hz, 47 dB over its floor, 0.923
standing) and consumed on air.

So it needs the `does-it-help` treatment rather than an edit:

- the refinement window must be at least one coarse step wide, or the coarse
  error is unrecoverable by construction;
- the coarse probe length is a constant to be **measured where it breaks**, not
  chosen -- the question is the shortest probe at which the weakest line in the
  corpus is still the winner of the coarse stage;
- every real-capture invariant has to come back identical, not close;
- and the win should be measured on air too, since the survey's confirmation
  pass pays this per candidate.

## What must be checkable

That a weak line is still found. The corpus has strong ones; the case this
optimisation can break is a line near the coarse stage's noise, and there is no
synthetic check for it today. That check is worth having **before** the change,
because it is the one that would fail.
