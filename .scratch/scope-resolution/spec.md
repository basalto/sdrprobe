# Choosing the Scope's frequency resolution

## What is asked for

A way to change the FFT size from the Scope views, affecting only those views.

## What is there now

`SDR_DSP_FFT_SIZE` is 2048, fixed at compile time, and the number of windows
averaged is *derived* from it: `windows = pair_count / SDR_DSP_FFT_SIZE`. A
block is 131072 pairs, so today that is exactly 64 non-overlapping windows
covering the block with nothing left over.

At 2 MS/s that gives 977 Hz bins.

## Three findings, and the first two are the useful ones

**The cost is nearly flat.** Measured: the spectrum stage is 4.35 ms a block,
6.6% of the 65.5 ms budget, for 64 windows of 2048 points. Because the window
count falls as the size rises, the total work is about `pairs x log2(N)` --
so 16384 points costs `14/11` of what 2048 does, around 5.5 ms. **Changing the
size is essentially free.** Any plan that treats this as a performance
trade-off is solving the wrong problem.

**The real trade is resolution against averaging.** Finer bins mean fewer
windows to average, so the trace gets noisier as it gets sharper:

| points | bin at 2 MS/s | windows averaged |
| --- | --- | --- |
| 512 | 3906 Hz | 256 |
| 1024 | 1953 Hz | 128 |
| 2048 | 977 Hz | 64 |
| 4096 | 488 Hz | 32 |
| 8192 | 244 Hz | 16 |
| 16384 | 122 Hz | 8 |

That is worth exposing precisely *because* it is a trade rather than a free
improvement, and the chart should say which end of it the reader is on. It
also pairs with the zoom the Scope has just grown: 122 Hz bins are wasted
across a 2 MHz axis and are exactly what a window 20 kHz wide wants.

**It is not the Scope's private FFT.** `spectrum_average[]` is written once a
block and read by five other things:

- the band survey, which folds it into a sweep's bins;
- the GSM channel scan, taking per-ARFCN powers from it;
- the FM band scan, taking per-channel powers on the 100 kHz raster;
- the calibration overlay, measuring a centroid and an FCCH tone in it;
- `survey_store` and `survey_report`, for the scripted paths.

Every one of them takes the bin count as an argument already, so they would
not *break*. They would quietly change: their thresholds, floors and
prominences were chosen against 977 Hz bins. "Applicable only on scope views"
therefore has to mean the others keep 2048, not that they happen to still
compile.

## The shape that follows

A second Scope-only array was the first plan. The better one, and the one
adopted: **there is one spectrum, and its size is a function of what is on
screen.** The consumers only ever read it while their own screen is up, so as
long as the size is the default whenever one of them can run, they see exactly
what they see today -- one array, one transform, no extra memory.

The refinement that makes it safe: the size is **derived every block from the
current screen, not reset when screens change**. Three separate bugs this week
were a transition that stopped being taken when the survey became a tab, and
"remember to put the size back" is the same shape of mistake waiting to
happen. A predicate -- the Scope tab is up, showing a frequency chart, with no
overlay over it -- answers it from state that is already true, and there is no
transition to miss.

The case a tab test alone would miss: **calibration is an overlay, not a
view**. It can sit over the Scope tab and it measures a centroid and an FCCH
tone in the spectrum, and so can the GSM channel scan inside it. Both are in
the predicate.

## The risk worth naming before any of it

**The hand-written FFT has only ever run at 2048 points.** It is a radix-2
loop and it *should* generalise, and "should" is what
`.claude/skills/dsp-validation` exists to distrust. Before anything is drawn
at a new size, a check has to put a synthetic tone through every offered size
and assert it lands in the right bin at the right level. A transform that is
subtly wrong at 8192 would show as a spectrum that looks plausible and is not.

## The ambiguity, settled

"Number of steps" meant the FFT size, confirmed 2026-09-04. The averaging
follows from it and is not separately controlled: separating them needs
overlapping windows -- averaging 64 windows of 8192 points needs four times
the samples a block holds -- and an overlap changes what the average means.
Not ruled out, not in scope.
