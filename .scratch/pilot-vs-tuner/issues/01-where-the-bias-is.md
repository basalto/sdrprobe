# 01 — Find which of the three is wrong

Status: ready-for-agent
Blocked by: (none)

The spec has the measurements. This is the work.

## First, narrow it without hardware

`check-fm-dsp` already sweeps the estimator across -60 to +60 ppm of sample
clock error under three audio levels and finds it good to 12. Extend that to
the scale actually in question -- if the estimator has no bias at -90 ppm
either, the estimator is not the problem and the receiver is.

The one place a systematic error could hide that the sweep would not catch is
the pilot loop pulling towards a neighbour: the stereo subcarrier at 38 kHz is
strong and 19 kHz away, and a 10 Hz loop with an asymmetric skirt would sit
slightly off. Feed a multiplex with and without a stereo subcarrier and
compare -- a bias that appears only with one is the answer.

## Then, with hardware

Record the same FM station at several applied corrections (0, 20, 35, 50, 70)
and plot the pilot reading against them. It should be a straight line of slope
-1 through the point where the sample clock is right. Where that line crosses
zero is what the sample clock actually wants, and comparing it to the +35 that
GSM and LTE agree on is the whole question.

If the line's slope is not -1, librtlsdr is applying the correction to the
resampler with a different scaling than to the tuner, and that is the answer
and is worth writing down in `fm_dsp.c` where the wrong assumption currently
sits.

## What to do with the answer

- **The estimator is biased**: fix it, or stop reporting the number. A figure
  on screen that disagrees with two better ones and cannot be explained is
  worse than no figure.
- **The receiver really is like this**: say so in `fm_dsp.c`, whose comment
  currently claims the two are usually equal, and consider whether the sample
  clock is worth reporting separately in the calibration overlay -- it is a
  real property of the receiver and nothing else here measures it.

Either way `fm_dsp.c`'s comment is wrong as written: it says
`set_freq_correction` leaves the sample clock alone, and the measurement in
the spec shows it moves by 30 ppm for a 35 ppm change.
