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

## The four ways an A/B here has gone wrong

**Measuring where the answer cannot show.** A shaped RDS filter was compared
against the rectangular one on a strong station and scored 153 aligned
syndrome hits against 153. That was not a wrong measurement, it was an
uninformative one: a strong station decodes either way and a decibel cannot
show against 20 dB of margin. **Measure where the thing being changed is the
binding constraint.**

**An instrument no finer than the effect.** A tool was written to sweep a tone
detector across a channel and used one knob for both the sweep step and each
probe's search width. Resolving a 2 kHz effect therefore meant stepping in
2.5 kHz strides, so the answer came out quantised to the size of the thing
being measured -- and it read a clean, monotonic 3.6 kHz trend across four
recordings that was mostly the stride. Separating the two knobs moved every
point by up to 800 Hz and halved the trend. **Before believing a difference,
ask what the instrument's own resolution is and confirm it is well under the
difference** -- and be most suspicious when the effect comes out close to a
step, a bin width, or a window length, because that is what an artefact of the
method looks like. A monotonic ordering is not protection: four points fall in
order by chance one time in twelve.

**Comparing at different gains.** The shaped filter's taps sum to half the
rectangular one's energy, so without a root-two scaling it reads uniformly
quieter and the comparison measures the scaling rather than the shape.
**Normalise whatever is not the thing under test.**

**One draw of noise.** A single seed at a single ratio can move a block count
further than the effect being looked for. Six draws per point, same draw to
both sides, or the comparison measures the noise.

**Timing two binaries on a machine that is not holding still.** This is the
one that wastes an afternoon, because every number looks plausible. The CPU
governor here is `powersave`, so the clock ramps with load and thermal state,
and `make bench-dsp` alternated three times each read a stage +13.7% that was
really -2.3%.

Before believing any before/after timing, **run the null experiment: the same
binary against itself**, split the runs the same way you split the comparison.
Measured on this machine, one binary against itself drifted a **median 16.1%
and up to 26.8%** between its early and late runs. Any difference smaller than
that is not a difference. Counterbalancing the order does not rescue it, and
neither does pinning with `taskset` -- both were tried, and the residual "5.9%
regression" they left was uniform across stages the change had not touched,
which is the tell.

**The fix is to put both versions in one process and alternate them**, so the
two timings share a thermal state, a frequency, and a cache. Old and new
`convert_iq`, sixty rounds, minimum of each: +0.6%, then -1.5%, -2.0%, -1.5%
across repeats -- a clean null, from the same code the cross-binary method had
called a 14% regression. `perf stat` on instructions retired would be better
still and is not installed here.

Related trap: **a divide by a variable is not slower than a divide by a
constant** in a loop where the divisor is invariant. Replacing `/ 127.5f` with
`/ full_scale` in the FFT window scale measured +0.3%, because the compiler
hoists either one into a register and cannot turn the constant into a
reciprocal multiply anyway -- 1/127.5 is not exact in binary and there is no
`-ffast-math`. Do not pay for a "fix" to this without measuring it first.

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

## A refactor is an A/B too, and the answer has to be *no difference*

Moving a decision out of a view and into a module is a change like any other,
and the measurement is the same shape run backwards: the number to compare is
one the program **already prints about itself**, and the result you want is
that it did not move.

`make check` cannot supply it. The survey's machine was pulled out of
`view_survey.c` with all 55 suites green, both capture surveys
**byte-identical**, and the survey screen rendering byte-identical -- while the
settle that throws away stale blocks was disabled. Captures never retune, so no
capture can exercise it, and the identity a capture pins is exactly the thing a
broken settle does not change.

What caught it was one line of the program's own output on a live sweep:

    survey blocks 26 settling 13     before
    survey blocks 40 settling  0     after

**So find the self-report before starting.** `survey blocks N settling M`,
`decoded N agreed M`, a funnel, a block count, a message count: every one of
these is a count the program prints about its own working, and a refactor that
moves one has changed something. Two of the four faults in that extraction were
found this way and neither was reachable any other way -- the second, a step
that could only end when a block arrived, showed up as `blocks 39` on the fix
for the first.

And **run the case the corpus cannot reach.** For anything that retunes, that
is a receiver: a capture holds one tuning, so every settle, every stale block
and every step boundary is untested by `testfiles/`. The same asymmetry applies
wherever the corpus is narrower than the program -- one gain, one rate, one
site.

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
