# One crystal, two answers

**Resolved 2026-09-04. See issues/01 for the answer: a real estimator bias
from the stereo subcarrier, and underneath it a question that had no answer
because a broadcast pilot is not a frequency reference.**

## What was noticed

Flagged while building the FM pilot loop and never filed, which is how it came
back: `fm_pilot_ppm()` measures this receiver's error against a broadcast
pilot and does not agree with what GSM and LTE measure against a cellular
carrier. The code comment in `fm_dsp.c` says the two are "usually equal on an
RTL-SDR, because one crystal feeds both". On this receiver they are not.

## What was measured, 2026-09-04, home-sala-estar, telescopic

All within a few minutes of each other, one receiver, one session.

| reference | what it measures | result |
| --- | --- | --- |
| GSM FCCH, ARFCN 113 | an offset at the tuned frequency | residual **-0.63 ppm** over 831 measurements, SEM 0.07, suggests **+36** |
| LTE cell search, EARFCN 6200 | an offset at the tuned frequency | residual **+0.09 ppm** over 421 measurements, SEM 0.01, suggests **+35** |
| FM pilot, 94.4 MHz, +35 applied | the sample clock, at baseband | **-51.3 ppm**, then **-60.6 ppm** on a second recording |
| FM pilot, 94.4 MHz, **0** applied | the same | **-90.9 ppm** |

Two things fall out of the last two rows.

**`--ppm` moves the sample clock as well as the tuner.** Changing the applied
correction from 0 to 35 moved the pilot reading by 30 ppm. That is librtlsdr:
`rtlsdr_set_freq_correction` sets the tuner's correction *and* the RTL2832's
resampler ratio. Worth knowing, and not what the comment in `fm_dsp.c`
assumes.

**The two references still disagree by tens of ppm.** With no correction
applied the sample clock reads about -91 ppm, while GSM and LTE both say the
tuner needs +35. One crystal cannot be 35 ppm off for one and 91 for the
other, so at least one of the three is biased.

## Which to suspect

GSM measures a pure tone at RF over 831 measurements with a standard error of
0.07 ppm, and LTE agrees to a tenth of a ppm by an entirely different route --
a Zadoff-Chu correlation and an integer-subcarrier search. Two independent
methods agreeing to 0.7 ppm is a strong claim.

The FM number is the weak one and is already documented as such: measured to
about +-10 ppm synthetically, and the two recordings above differ by 9. But
+-10 does not explain a gap of 55.

## Why it matters

Not because the FM number is used for anything -- it is reported and never
fed to the calibration gate, which is correct. It matters because
`fm_pilot_ppm()` is on screen next to two numbers that disagree with it, and a
reader has no way to tell which to believe. Either it is right and something
about this receiver is more interesting than a crystal, or it is biased and
should say so or go.

## What would settle it

A frequency this program does not have to trust: the FM pilot's own accuracy
is held to +-2 Hz by the broadcast standard, which is 105 ppm *of the pilot*
and says nothing useful. Measuring the same station's pilot against a
calibrated reference, or measuring a signal generator, would.

Failing that: feed the FM chain a synthesised multiplex at a *known* sample
rate error and confirm the estimator has no bias at the tens-of-ppm scale --
`check-fm-dsp` sweeps -60 to +60 ppm already and finds none, which is evidence
against the estimator and for something real.
