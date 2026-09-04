# 01 — Find which of the three is wrong

Status: resolved
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

## Comments

**2026-09-04** — Resolved. Two separate things were wrong, one in this program
and one in the question.

**The estimator had a real bias, and it is fixed.** The pilot loop's
correlator multiplies the whole multiplex by its own oscillator, so a station
broadcasting in stereo -- a subcarrier at 38 kHz with sidebands reaching down
to 23 -- dragged it. Synthesised, the error across +-120 ppm ran from -24 to
+21 with a subcarrier present against -3 to +2 without, and under loud audio
reached +33. A two-pole resonator at 19 kHz in front of the loop takes it to
-1.0 to +0.8, and the sweep in `check-fm-dsp` is tightened from 12 ppm to 4.
`test_stereo_does_not_drag_the_pilot` compares the same multiplex with and
without the subcarrier, which is the only comparison that measures the pull
rather than the estimator against itself.

The resonator turns its input by ninety degrees at resonance, and that phase
is tripled for the RDS subcarrier and doubled for the stereo difference -- so
a hundred and eighty degrees there, and the two audio channels came out
swapped. They did, and the stereo separation check caught it. The turn is
computed in init and taken back off the phase the loop hands out.

**The question was wrong.** After the fix, a mono station read +2.0 ppm and a
stereo one still read -57.6, so the remaining gap was never the estimator.
Five stations, one receiver, one clock, each recorded twice:

    89.5  +1.93, +1.98      93.2  -18.98, -17.69     94.4  -57.77, -56.82
    97.4  -15.72, -16.34   100.3  -46.82, -46.48

Repeatable to about a ppm within a station, spread over 59 between them. One
clock cannot be five different amounts wrong. The spread is the transmitters,
and it is well inside their rights: a pilot is held to +-2 Hz, which at 19 kHz
is +-105 ppm.

So `fm_pilot_ppm` measures the transmitter's pilot against this receiver's
clock and cannot separate the two. The comment claiming otherwise is
rewritten, and the view says "pilot offset" where it said "sample clock".
GSM and LTE, measured the same afternoon, agree with each other to 0.7 ppm --
which is the half of this that was always working.

**One thing kept from the way there:** applying a correction moves the sample
clock as well as the tuner, 30 ppm of movement for a 35 ppm change, because
librtlsdr's `set_freq_correction` writes the RTL2832's resampler ratio too.
