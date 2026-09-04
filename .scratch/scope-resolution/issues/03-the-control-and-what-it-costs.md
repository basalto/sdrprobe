# 03 — The control, and saying what it costs

Status: needs-triage
Blocked by: 02

## Where it goes

Three candidates, and the choice is not obvious:

- **The Scope header**, beside the frequency window's fields
  (`.scratch/frequency-window/`). It changes what the chart shows, which is
  what the rest of that row is about. Against: that row is already full, and
  this is set once and left, unlike a frequency.
- **The Settings panel.** It is a property of the instrument rather than of
  what is being looked at. Against: ticket 04 of the frequency work is busy
  taking the centre frequency *out* of Settings on the grounds that a thing
  people change often should not need an overlay -- and resolution is
  changed rarely, so that argument does not apply here.
- **A key.** Cheap and undiscoverable.

Settings is probably right for exactly the reason the centre frequency is
leaving it: rarely changed, describes the instrument.

## What it must say

The trade, not just the number. Finer bins mean fewer windows averaged and a
noisier trace, and a reader choosing 16384 should be told they have gone from
64 averages to 8 rather than discovering it as a trace that suddenly looks
worse. The spectrum's caption already reads "64 x 2048-point FFT ... bin
976.562 Hz" -- it says all three numbers today and simply needs to keep being
right.

## Worth deciding here

Whether the choice persists in the config the way the site and the antenna do.
It describes how somebody likes to work rather than a run, which is the test
`config.h` applies -- so probably yes.
