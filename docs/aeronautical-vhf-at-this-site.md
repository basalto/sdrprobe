# Aeronautical VHF at this site: an hour on air, and what was there

Measured 2026-09-11, 10:09 to 11:05 local, a Friday morning. R820T on a
telescopic whip indoors, gain 29.7 dB, site `home-sala-estar`.

**The question.** `.scratch/am-airband/` proposed an airband occupancy record
and an AM demodulator, gated on ticket 01: a daytime capture with a
transmission in it, the only previous one having been taken at 01:05 with
nothing on air. This is that hour.

**The answer.** Nothing in 108-137 MHz here is external. Every signal the
survey raises is either a carrier clocked with the receiver's own crystal or a
noise maximum with no carrier at all. The gate fails, and it fails on the
*installation* rather than the hour, which is a stronger and less recoverable
result than a night-time null.

---

## 1. What was run

| | |
| --- | --- |
| swept survey, 118-137 MHz, 0.3 s dwell, `--survey-confirm` | 18 candidates, 12 confirmed, 4 refuted, 2 intermittent |
| swept survey, 118-137 MHz, 2.0 s dwell | 20 candidates |
| three captures of 90 s at 121.3, 134.3, 129.4 MHz | 1.08 GB, analysed and deleted |
| AM prototype (`.scratch/am-airband/am_probe_prototype.c`) at five channels | whole-look and per-second |
| `probe-signal` at every candidate, several channel widths | the shipped, checked path |
| GSM ARFCN 113 calibration | the receiver's ppm, today |
| swept survey, 108-118 MHz, 2.0 s dwell, `--survey-confirm` | the control |

The captures were deleted afterwards: they hold no transmission, so they
cannot become test fixtures, and every number taken from them is below.

## 2. The receiver's ppm, measured and not assumed

```
calibrate-result locked 1 measurements 498 centre_ppm -35.96 sem_ppm 0.04
                 spread_ppm 0.33 suggested_ppm 36 reason locked
```

**-35.96 ppm.** This is the whole basis of what follows, because the survey
reports frequencies on the nominal tuning grid: a crystal error `d` displaces
every reported frequency by `f_tuned * d`, which here is **4.0 kHz at 110 MHz
and 4.9 kHz at 136 MHz**. An external transmitter therefore *cannot* be
reported at its true channel. A source clocked from the same crystal has that
displacement cancel and reads at its exact nominal frequency -- the derivation
is in `SIGNAL_COARSE_PAIRS`' neighbourhood in `signal_probe.h` and in
`.scratch/device-model/issues/11-*`.

So "reads exact" and "reads 4 kHz off" are the two classes, and they are not
close together.

## 3. Every line found, and what it is

Frequencies from the 2.0 s-dwell sweeps, which resolve them; the 0.3 s sweep's
coarse bins misplaced several by 10-25 kHz.

| measured | offset from exact | is | dB over floor |
| --- | --- | --- | --- |
| 120.000427 | +427 Hz | **1.6 MHz x 75** | 48.0 (70 at one step) |
| 127.999817 | -183 Hz | **1.6 MHz x 80** | 23.5 |
| 129.600159 | +159 Hz | **14.4 MHz x 9** | 27.3 (49.4 measured) |
| 135.999207 | -793 Hz | **1.6 MHz x 85** | 25.5 |
| 110.400513 | +513 Hz | **1.6 MHz x 69** | 46.0 |
| 115.197876 | -2124 Hz | **1.6 MHz x 72** | **70.3** |
| 129.999999 | **-1 Hz** | no modelled comb | 27.2 |
| 134.999987 | **-13 Hz** | no modelled comb | 44.7 |

Everything else the sweep raised reads **no carrier** when measured:
121.808 (7.4 dB), 128.926 (12.9), 134.502 (2.6), 133.544 (4.0), 125.121 (8.9),
114.119 (10.3), 108.974 (14.9). These pass the survey's *prominence* bar --
several were confirmed 6 of 6 looks -- and have no standing carrier at all.
`signal_probe.h` explains why that is expected rather than surprising: a
search over thousands of frequencies takes the largest of thousands of noise
samples, and pure noise reliably produces a "line" at 8 to 14 dB over its
median floor.

## 4. What 130.000000 and 135.000000 are

They fit neither the 14.4 MHz comb nor the 1.6 MHz one, and the survey called
130.014160 **"a modulated carrier"**, which reads like a service rather than a
spur. They are not modulated. They are **bare carriers**, and the verdict is
an artefact of the channel they were measured in.

`signal_carrier_verdict()` calls a line modulated when less than
`SIGNAL_BARE_FRACTION` (0.80) of the **channel's** power stands still. A narrow
line in a wide channel therefore reads modulated however pure it is, because
the channel's noise grows with its width while the line does not. Measured at
130.000000 across five widths:

| channel | standing, measured | predicted for a *pure* line in noise |
| --- | --- | --- |
| 25 kHz | 0.261 | 0.229 |
| 12.5 kHz | 0.392 | 0.372 |
| 5 kHz | 0.597 | (calibration point) |
| 2 kHz | 0.763 | 0.787 |
| 1 kHz | **0.851 -- "a bare carrier"** | 0.881 |

The model is one line -- `standing = P / (P + n*W)` for a line of power `P` in
noise of density `n` over width `W` -- calibrated at 5 kHz and then predicting
the other four to within 0.03. There is no modulation to find. 135.000000 is
the same signal 18 dB stronger, so it reads bare as soon as the channel is
5 kHz (standing 0.934, envelope variation 0.215, which is inside
`signal_probe.h`'s 0.137-0.248 bare-carrier range); the survey's own pass
called it bare at 0.846.

**So both are unmodulated clock spurs**, and three things say they are the
receiver's rather than anything outside it:

- they read **1 Hz and 13 Hz** from exact nominal, where an external signal
  must be 4.7 kHz off;
- they are continuous -- `signal_find_bursts()` reports `level`, occupancy
  0.0000 -- and airband voice is not;
- the strong members of the same family were present in the 00:24 night sweep
  at the same strength, so they do not follow the clock.

**A limitation this exposes, and it is worth acting on.** The survey's
confirmation pass uses the carrier's *measured bandwidth* as its channel
(`survey_session.c:760`), which on a coarse sweep is about a bin. The
consequence is systematic: **a weak bare line reads "a modulated carrier"
unless the channel is narrow enough**, and "modulated" is exactly the word
`signal_findings.h` says must not become a verdict, because a reader acts on
it. The verdict is a statement about line-to-noise within a channel, not about
the transmitter.

## 5. No transmission, in any second

The AM prototype over the three captures. Its positive control fired in the
same data -- 129.600000 read carrier 0.0067 against its own 0.0015 controls,
almost exactly the 0.0064 it reads on `testfiles/carrier_75000_bare.bin` --
which is what makes the nulls worth reading.

Largest per-second carrier excursion over the quietest second of 90:

```
121.808  0.6 dB      128.926  0.6 dB      129.601 (comb)  0.3 dB
135.025  0.3 dB      130.014  1.8 dB
134.502  0.5 dB
```

`.scratch/am-airband/spec.md` records an *idle* channel varying 0.6 dB over
20 s. Nothing here stands clear of that. Speech-band excess -- 300-3400 Hz
against 4-8 kHz, the RDS two-band trick -- was **negative at every offset**,
-0.5 to -2.0 dB, signal and controls alike.

## 6. The control, which is what makes this a finding

A null over voice channels proves little: voice is intermittent, and 4.5
minutes of listening can miss it. So the band below was swept instead.

**VOR and ILS beacons transmit continuously.** 108-117.975 MHz is full of
them wherever there is aviation, and they cannot be missed for being off the
air. The sweep found its only two **bare carriers** at 115.197876 (70.3 dB)
and 110.400513 (46.0 dB) -- both 1.6 MHz comb multiples, both the receiver.
Everything else read standing 0.02 to 0.49 with no carrier signature, and none
sat a channel-plus-4.1-kHz from any VOR frequency.

So this is not "no traffic during that hour". Across the whole of aeronautical
VHF, **this receiving setup hears nothing external at all** -- while the same
antenna, twenty megahertz lower, hears ten FM broadcast carriers at up to
55.9 dB prominence. The equipment works. It is deaf at 110-137 MHz, and its
own spurs are the loudest thing in the band by about 20 dB.

## 7. What follows

**`.scratch/am-airband/` ticket 01's gate fails on the installation.** More
captures at this site will not pass it: an indoor telescopic whip at 120 MHz
has no ground plane and no line of sight, and a 70 dB internal spur sits in
the middle of the band. Tickets 02 (occupancy) and 03 (the demodulator) stay
closed. What would change the answer is an outdoor antenna or a site within
useful range of an airfield; if neither is coming, the honest close is
`wontfix`.

`spec.md` says "three of the six strongest things in the airband are the
receiver". It is understated: all of them are, and the one candidate the
earlier analysis left standing -- 132.062744, which fitted an 8.33 kHz channel
-- did not reappear today.

**`.scratch/device-model/issues/10-*` gains four comb members.** The
unexplained clock-coherent lines are now 75, 130, 135 and 150 MHz. Every one
is a multiple of 5 MHz and none of 1.6 or 14.4 -- but 110, 115, 120 and 125
are also multiples of 5 and carry no unexplained line, so "a 5 MHz comb"
predicts lines that are not there and is not yet the answer. What is solid is
that 5 MHz is not 28 800 000 / n, so whatever produces them is not a plain
divider off the reference.

## 8. Two instrument notes

**Two probes appeared to disagree by 30 dB and neither was wrong.**
`am_probe` read control-level at every candidate while `signal_probe` reported
25-45 dB carriers. The prototype had been aimed at the *candidate* frequency
while the real lines sit up to 25 kHz away; `probe-signal`'s `line found at:`
field is what exposed it. Without that field the two readings look like a
contradiction and the weaker instrument wins by being simpler.

**The coarse bin is why any of this looked like traffic.** At 2319 Hz the
0.3 s sweep placed 135.000000 at 135.025085 -- which is a real 25 kHz airband
channel, plus 85 Hz -- and 130.000000 at 130.014160, and 127.999817 at
127.985901. A channelised scanner trusting the sweep's frequency rather than
the measured one would report an ATC channel permanently busy. That is
`.scratch/am-airband/issues/02-*`'s first bug, and it is avoidable by
construction: the measurement is the better frequency and
`survey_record_candidates()` already prefers it.
