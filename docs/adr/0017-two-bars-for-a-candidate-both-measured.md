# A candidate clears two bars, and the extent rule stays

## Status

accepted. Supersedes ADR-0013 on the noise measurement and the interface;
keeps its decision on the filtering, for a reason ADR-0013 did not give.

## Context

ADR-0013 recorded three things about `sdr_dsp_find_peaks()` and left all of
them:

- the gate is `topographic_prominence()` and the reported *above floor* figure
  is `power - local_floor()`, which are different measurements, so a candidate
  can clear the first and report much less of the second;
- the real threshold was nearer 20 dB than the 8 dB asked for, because a
  candidate with no `-bandwidth_db` point had its width walk run to the ends of
  the array, `local_floor()` was then left nothing outside the "hump" to take a
  median of and returned NaN, and the candidate was discarded for having no
  floor;
- so the filtering an explicit threshold should have done was being done by an
  accident whose level nobody chose.

It left them because of a noise measurement -- 16 dB from peak-held noise at
one block -- and said that doing it properly meant "separating the two
prominences, choosing a threshold from the dwell, and re-checking the candidate
counts against a live sweep."

**The noise measurement is wrong.** `make probe-survey-threshold` puts pure
complex noise through the real transform and the real fold and looks:

| sweep | bins | fold depth | array sd | worst bin-to-bin step | candidates |
| --- | --- | --- | --- | --- | --- |
| 2 MHz band, 0.1 s | 1 kHz | 1 | 0.57 dB | 2.2 dB | 0 |
| 20 MHz band, 0.1 s | 2 kHz | 2 | 0.47 dB | 2.7 dB | 0 |
| 216 MHz band, 0.1 s | 26 kHz | 27 | 0.27 dB | 1.7 dB | 0 |
| 220 MHz band, 0.2 s | 27 kHz | 82 | 0.23 dB | 1.4 dB | 0 |
| whole tuner, 0.1 s | 213 kHz | 218 | 0.20 dB | 1.3 dB | 0 |

Not one candidate, at any fold depth, down to a bar of zero. The averaging
inside `sdr_dsp_spectrum()` is why: every transform bin is the mean of all 64
Hann windows in the block, and a peak hold over a few hundred such means barely
moves. ADR-0013's table has to have been taken on an unaveraged periodogram,
where an exponential per bin does reach 16 dB. The survey does not fold one of
those.

So there is nothing for a dwell-derived threshold to track, and the accident
was not holding back noise. That left the obvious conclusion -- that it was
holding back nothing, and could go. **It was measured, and it was wrong.**

## Decision

Two bars, both explicit, carried together in `struct sdr_peak_gate`:
`topographic_db`, the descent before higher ground can be reached; and
`floor_db`, height above the median level around it. The survey sets both to
8 dB, so a candidate cannot clear the gate and report less than it -- which is
the half of ADR-0013's complaint that had a clean fix. One struct rather than
two float arguments because two adjacent floats are swappable without a
compiler complaint, and swapping them leaves a gate that still runs.

**And the extent rule stays**, unbounded walk and NaN and all, now with a
reason rather than as an accident: a candidate with no `-bandwidth_db` point of
its own has no measurable extent, and something with no end to it is a feature
inside something larger rather than a signal in its own right.

## Consequences

The alternative was implemented and measured before this was written, because
the argument for it was good and the measurement is what settled it. Bounding
the walk and measuring the floor outward from the hump's edges -- which is what
`local_floor()`'s name always suggested, and which removes the silent NaN --
gives, on three live sweeps of 470-690 MHz at a 0.2 s dwell:

| | candidates | carriers |
| --- | --- | --- |
| the extent rule | 39, 40, 40 | 38, 40, 39 |
| bounded walk, floor outside the hump | 59, 61, 61 | 58, 60, 60 |

**All 28 additions are inside a DVB-T channel's 8 MHz**: ripple on the flat top
of a television multiplex, each with a real descent either side, each becoming
a carrier of its own. Twenty television multiplexes reported as sixty signals
is worse than the fault it fixes, and the carrier count is the number a reader
uses.

A third variant -- bound the walk and require it to *close* -- removes the
ripples cleanly (45 candidates to 25 on the same band, all 20 removals inside a
channel) and takes real signals with them: a bare tone at 102.4 MHz standing
18 dB above its floor, the 1090 MHz carrier in `testfiles/adsb_cpr_pair.bin`
that yields six decoded frames, and ARFCN 63 in `testfiles/gsm_arfcn_69.bin`,
which the cell's own System Information 2 names as a neighbour. Nothing
measured here tells those apart from a ripple: both are maxima whose extent
does not close.

So the cost of keeping the extent rule is now known rather than assumed, and it
is real. `check-pipelines` asserts both halves of it -- one carrier from the
GSM capture and none from the Mode S one -- with the reason written beside
them, so recovering them shows up as a change rather than as luck.

What would recover them is a different measurement, not a different threshold:
walking to the *trough* rather than to a fixed number of decibels down.
`survey_carrier_edge()` in `src/survey_carrier.h` already does exactly that for
the carrier grouping, and it separates the two cases by construction -- a
ripple's trough is the notch beside it, inside the multiplex, while an isolated
tone's is the noise. `.scratch/survey-extent/` is that work.

Also settled here, and smaller: a maximum whose median local floor is above it
is dropped rather than reported with a negative prominence. `prominence_db` is
now always above zero.

The stated 8 dB is still not the effective bar in a busy band -- the extent
rule is, at nearer 20 dB. The difference from ADR-0013 is that it is now a rule
with a name and a reason, the reported figure can no longer contradict the
gate, and the noise claim underneath it all has been measured instead of
assumed.


## 2026-09-06: bounding the walk alone, without touching the floor

The variant measured above was **two** changes -- bound the walk *and* measure
the floor outward from the hump's edges -- and this records that the second is
what admitted the ripples. Bounded walk, `local_floor()` left exactly as it
was, three sweeps of 470-690 MHz at a 0.2 s dwell:

| | candidates | carriers |
| --- | --- | --- |
| the extent rule | 36, 34, 35 | 36, 34, 35 |
| bounded walk, floor rule unchanged | 42, 43, 41 | 42, 43, 41 |
| bounded walk **and** floor outside the hump | 59, 61, 61 | 58, 60, 60 |

Seven more rather than twenty-one, and the carrier count tracks the candidate
count exactly -- nothing clustered inside a multiplex. Checked directly: of 42
candidates, **none** sits inside a wider, stronger carrier.

The mechanism is that the two halves filter different things. A ripple on a
multiplex has a hump far wider than an isolated tone's, so its floor window --
eight times the width -- still does not fit beside it, `local_floor()` still
returns a NaN, and it is still dropped. Measuring outward from the hump's edges
is what removed that filter. The bound only ever needed to leave *something*
outside the hump, and a quarter of the array does.

All three signals ADR-0017 recorded as the cost come back: the 102.4 MHz tone
at 17.6 dB of prominence, the 1090 MHz carrier in `adsb_cpr_pair.bin` at
1089.96 MHz, and ARFCN 63 in `gsm_arfcn_69.bin`. `check-pipelines` asserts the
first two by name now, where it used to assert their absence.

**So the decision above is reversed on its narrow point** -- the unbounded walk
and the NaN are gone -- and upheld on the wide one: a fixed threshold is still
the wrong instrument, and what settled this was measuring each half of a change
separately rather than the pair.
