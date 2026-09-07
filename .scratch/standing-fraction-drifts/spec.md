# The standing fraction falls with the length of the look

`carrier_power_fraction` is how much of a channel stands still, and it decides
whether the panel says "a bare carrier" or "a modulated carrier". Measured on
`testfiles/carrier_75000_bare.bin`, which is a bare carrier:

| observation | pairs | standing fraction | verdict |
| --- | --- | --- | --- |
| 0.07 s | 131072 | 0.888 | a bare carrier |
| 0.20 s | 400000 | 0.921 | a bare carrier |
| 0.50 s | 1000000 | 0.918 | a bare carrier |
| 1.00 s | 2000000 | 0.890 | a bare carrier |
| **2.00 s** | 4000000 | **0.779** | **a modulated carrier** |

The same signal, and the verdict flips at `SIGNAL_BARE_FRACTION`'s 0.80.

## Why

`constant_fraction()` mixes at one fixed frequency and takes the mean of every
decimated block in the buffer. A carrier drifting even a fraction of a hertz
walks out of phase across a long observation, so the global mean cancels
against itself -- and cancellation reads exactly like modulation, because
modulation is what the statistic is looking for.

An uncalibrated R820T drifts. `fm_pilot_ppm` records five stations spreading
59 ppm at 19 kHz; at 75 MHz a tenth of a part per million is 7.5 Hz, which is
fifteen full turns in two seconds.

## What is not affected

**No shipped path hits it.** Both callers hand it one block: the window's
measure path (`survey_measure_carrier`) and the confirmation pass
(`survey_confirm_measure_kind`) pass `app->pair_count`, which is 131072 --
inside the flat region. The only caller that reaches the falling part is
`make probe-signal`, which is how this was found.

So this is a latent defect rather than a live wrong answer, and worth fixing
before something hands it more signal.

## The fix, and why it needs its own measurement

Take the mean over *segments* short enough that the drift is negligible, and
average the per-segment fractions -- which is the same trade `signal_shape`
already made and for the same reason.

**That changes the statistic for every caller.** `SIGNAL_BARE_FRACTION`'s 0.80
was measured against the unsegmented form: a synthetic tone 1.00, the 75 MHz
harmonic 0.87, a modulated carrier under 0.01, TETRA and FM near zero. Every
one of those has to be re-measured, because a segmented mean will not give the
same numbers -- it cannot, or it would not be a fix.

`make probe-signal` is the instrument for that table now.
