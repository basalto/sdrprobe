# A candidate clears two bars, and both of them are chosen

## Status

accepted. Supersedes ADR-0013.

## Context

ADR-0013 recorded three things about `sdr_dsp_find_peaks()` and decided to
leave all of them:

- the gate is `topographic_prominence()` and the reported *above floor* figure
  is `power - local_floor()`, which are different measurements;
- the real threshold was nearer 20 dB than the 8 dB asked for, because a
  candidate with no `-bandwidth_db` point had its width walk run to the ends of
  the array, `local_floor()` was then left nothing outside the "hump" to take a
  median of and returned NaN, and the candidate was discarded for having no
  floor;
- so the filtering an explicit threshold should have done was being done by an
  accident whose level nobody chose and which moved with the noise.

It left them because of a measurement: peak-held noise was said to reach 16 dB
at one block and 8.3 dB at sixteen, so making the 8 dB real would flood the
list -- and on a live 470-690 MHz sweep, bounding the walk took the survey from
38 candidates to 74, half of them under 10 dB.

That reasoning has a hole in it, and the hole is the noise measurement.
`make probe-survey-threshold` puts pure complex noise through the real
transform and the real fold and looks at what comes out:

| sweep | bins | fold depth | array sd | worst bin-to-bin step | candidates |
| --- | --- | --- | --- | --- | --- |
| 2 MHz band, 0.1 s | 1 kHz | 1 | 0.57 dB | 2.2 dB | 0 |
| 20 MHz band, 0.1 s | 2 kHz | 2 | 0.47 dB | 2.7 dB | 0 |
| 216 MHz band, 0.1 s | 26 kHz | 27 | 0.27 dB | 1.7 dB | 0 |
| 220 MHz band, 0.2 s | 27 kHz | 82 | 0.23 dB | 1.4 dB | 0 |
| whole tuner, 0.1 s | 213 kHz | 218 | 0.20 dB | 1.3 dB | 0 |

Not one candidate, at any fold depth, down to a bar of zero. The reason is the
averaging inside `sdr_dsp_spectrum()`: every transform bin is the mean of every
Hann window in the block, 64 of them at the house rate, and a peak hold over a
few hundred such means barely moves. ADR-0013's table has to have been taken on
an unaveraged periodogram, where an exponential per bin does reach 16 dB. The
survey does not fold one of those.

So there was never a flood to fear, and the 20 dB accident was not protecting
the list from noise. It was hiding signals.

## Decision

Two bars, both explicit, carried together in `struct sdr_peak_gate`:

- `topographic_db`, the descent before higher ground can be reached, which is
  what rejects the shoulder of a strong carrier;
- `floor_db`, height above the median level around it, which is what makes the
  reported figure mean what the help text says.

The survey sets both to 8 dB (`SURVEY_FLOOR_THRESHOLD_DB` is defined as
`SURVEY_MIN_PROMINENCE_DB`, not as its own number), so a candidate cannot clear
the gate and report less than it. They are one struct rather than two float
arguments because two adjacent floats are swappable without a compiler
complaint, and swapping them leaves a gate that still runs.

Both walks are bounded. The width walk stops at the same span the topographic
test judged over. And `local_floor()` now measures **outward from the hump's
edges** rather than in a window centred on the peak -- which is what its name
always claimed and what removes the NaN path for good. Centred, it had two
silent failures: a window narrower than the hump gathered nothing and returned
NaN, and a window barely wider gathered a handful of bins off the carrier's own
skirt and called that the noise.

## Consequences

Measured on air, three sweeps of each band, same antenna and gain:

| band | before | after | lowest reported prominence, before / after |
| --- | --- | --- | --- |
| 470-690 MHz, 0.2 s | 39, 40, 40 | 30, 29, 32 | 15.4 dB / 18.4 dB |
| 88-108 MHz, 0.1 s | 33, 30, 32 | 26, 28, 32 | 16.6 dB / 17.1 dB |
| whole tuner, 0.1 s | 234 | 217 | 7.3 dB / 8.4 dB |

Fewer candidates, not more. **All fourteen that the UHF sweep dropped fall
inside a DVB-T channel's 8 MHz**, and the old code had reported them at 19 to
27 dB "above their floor" -- a floor gathered from outside the multiplex they
were sitting on. A ripple on a television multiplex is not a signal standing
25 dB above the noise, and it is no longer described as one.

The whole-tuner sweep is where the old contradiction showed: one candidate
reported 7.3 dB above its floor while the gate it passed was 8 dB. Nothing
under the bar is reported now.

In the other direction, two captures gained a candidate each, and both are
corroborated rather than assumed:

- `gsm_arfcn_69.bin` now surveys to two carriers. The second is at
  947.6347 MHz, which is ARFCN 63 -- and the cell's own System Information 2,
  decoded from the same capture by an entirely different chain, lists 63 among
  its neighbours. `check-pipelines` asserts both halves.
- `adsb_cpr_pair.bin` now surveys to one carrier at 1090.10 MHz. The pipeline
  used to assert it surveyed to *nothing*, with the reason "Mode S is pulses:
  there is no carrier standing above anything" -- which was the old peak
  finder's behaviour written up as a fact about physics. A train of pulses is
  amplitude modulation on a carrier, the same capture yields six decoded
  frames, and the denser `adsb_modes1.bin` shows the same carrier 35 dB
  stronger. What that check was really asserting is that the survey does not
  invent candidates, and it now asserts that by where the candidate lands.

`sdr_peak.lower_index` and `upper_index` are now bounded, so on a carrier with
no `-bandwidth_db` point they report where the walk was stopped rather than
where the carrier ends. Nothing outside `sdr_dsp_find_peaks()` reads them; the
occupied width a survey reports comes from `sdr_dsp_characterise_carrier()` and
from the carrier grouping, both of which measure it properly.

The bar is still a constant rather than a function of the dwell. ADR-0013
expected it would have to be one; the probe is what says otherwise, and it is a
`make` target so the claim can be re-run rather than re-argued.
