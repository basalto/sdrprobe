---
name: does-it-help
description: Decide whether a change to the DSP or a capture actually improves anything, with a number. Use when comparing two implementations, when tempted to adopt a theoretically better algorithm, or when choosing a constant like a dwell, a chunk length or a capture duration.
---

# Better is a measurement, not an argument

`dsp-validation` answers *is this result true*. This answers *is this change
worth having*, which fails differently: the comparison can be perfectly
repeatable and still measure the wrong thing.

Close the question with a table, in either direction. A change that shows no
gain is worth writing down as showing no gain -- otherwise it comes back as a
suggestion every year with no data attached.

## The three ways an A/B here has gone wrong

**Measuring where the answer cannot show.** A shaped RDS filter was compared
against the rectangular one on a strong station and scored 153 aligned
syndrome hits against 153. That was not a wrong measurement, it was an
uninformative one: a strong station decodes either way and a decibel cannot
show against 20 dB of margin. **Measure where the thing being changed is the
binding constraint.**

**Comparing at different gains.** The shaped filter's taps sum to half the
rectangular one's energy, so without a root-two scaling it reads uniformly
quieter and the comparison measures the scaling rather than the shape.
**Normalise whatever is not the thing under test.**

**One draw of noise.** A single seed at a single ratio can move a block count
further than the effect being looked for. Six draws per point, same draw to
both sides, or the comparison measures the noise.

## When no weak signal exists, make one

Every RDS carrier reachable from this site either decodes well or carries
nothing -- 87.6, 95.3 and 96.6 each got twenty seconds and produced nothing.
So the weak signal was synthesised: add Gaussian noise to a real recording's
I/Q and sweep it.

**Noise goes on the I/Q, not on the baseband.** The FM discriminator is
nonlinear and has a threshold, so its output noise depends on the
carrier-to-noise ratio in a way that adding noise afterwards does not
reproduce. Adding noise downstream of a nonlinearity measures a signal that
never existed.

Expect a cliff, and step finely near it. FM collapses below about 10 dB and is
untouched above 13, so the whole interesting region is three decibels wide and
an evenly spread sweep walks straight past it.

`make probe-fm-filter` is the worked example; the probes in `scripts/` are the
shape to copy. A probe prints a table and decides nothing.

## Reading the result

**Separate the fine instrument from the outcome.** Blocks and groups move
before a name does. The shaped filter gained about 14% in blocks at the
threshold edge and did not clearly improve whether the station got *named* --
which is what a person sees. Report both; they can disagree.

**A gain inside a window nobody occupies is not a gain.** The shaped filter is
never worse and slightly better, and was still not adopted: the window where
the two differ is three decibels wide, and no station reachable from this site
lives in it. A change that cannot be observed here has regressions that cannot
be observed here either.

**Keep the losing branch reachable when the answer is site-dependent.**
`fm_rds_soft_bits_with()` and the probe stayed, so the default is one argument
away and the question is re-answerable rather than re-arguable.

## Choosing a constant is the same question

A dwell, a chunk length, a visit budget, a capture duration: measure the
boundary rather than picking a round number.

- Find where it breaks. One second of a capture names nothing; two seconds
  names it *sometimes*, depending on where the segment cycle falls; three is
  margin. The capture is three.
- State the cost so it stays checkable. `fm_scan_seconds()` exists because a
  third scan pass is only defensible while the two-pass arithmetic still
  holds, and charging the cap is what caught an eight-second budget putting
  the worst case past the minute it was built to beat.
- Prefer an early exit to a generous budget. The name pass stops the moment
  the name is confirmed, so a station that names itself in two seconds costs
  two and the cap is only paid by one that never manages it.

## Write the measurement down where it will be found

`docs/rds-matched-filter.md` is the pattern: the claim, why the first attempt
was uninformative, how it was measured, the table, and the reasons it was not
adopted anyway. A ticket comment carries the same content when the answer is
smaller. The table is the durable part -- the conclusion may not survive a
different antenna, and the numbers say why.
