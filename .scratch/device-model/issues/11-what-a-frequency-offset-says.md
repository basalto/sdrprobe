# 11 - What a candidate's offset from exact says about whose signal it is

Status: needs-triage
Opened 2026-09-10, from a review of `10-a-second-clock-nothing-models.md` and
`.scratch/am-airband/spec.md`. Blocks the useful half of both.

Ticket 10 asks *"whose 25 MHz is it"* and proposes two experiments -- move the
dongle to another room, and re-measure after a calibration -- on the grounds
that an uncalibrated receiver makes the readings uncertain by 4.6 kHz at
150 MHz, so *"they neither confirm nor deny a common fundamental"*.

**That is the wrong way round, and the answer is already in `testfiles/`.** The
ppm error does not blur a reading, it *displaces* it by a known amount -- and
the displacement is what tells an internal tone from an external one, because
an internal tone does not get displaced at all.

## The arithmetic

A survey reports a peak as `step_centre_nominal + bin * (rate_nominal / fft)`.
Let the crystal's fractional error be `d`. Then the actual local oscillator is
`f_step * (1 + d)` and the actual sample rate is `rate * (1 + d)`, so a tone at
true frequency `f_t` lands at baseband `f_t - f_step*(1+d)` and is reported at

```
f_reported = f_step + (f_t - f_step*(1 + d)) / (1 + d)
           ~= f_t - f_step * d          (to first order)
```

The second term is the whole of it: a **constant** displacement set by the
*tuning*, not by the tone. The baseband scale error is `d` times a sub-megahertz
offset -- under 31 Hz here -- and is negligible beside it. On this receiver at
`d ~= 31 ppm` that is **4.1 kHz at 132 MHz, 4.2 at 135, 4.6 at 150**.

Now put a tone that is *generated from the same crystal* through it. Its true
frequency is `f_nom * (1 + d)`, so

```
f_reported ~= f_nom * (1 + d) - f_step * d  =  f_nom + d * (f_nom - f_step)
```

and `f_nom - f_step` is the baseband offset, at most a megahertz. **The crystal
error cancels: a clock-coherent tone reads at its exact nominal frequency,
within tens of hertz, however far out the crystal is.** An external transmitter
cannot, unless its own oscillator happens to be wrong by the same `d`.

So the discriminator is one subtraction, and it needs no room change:

| reads | is |
| --- | --- |
| within a few hundred hertz of an exact multiple / an exact channel | coherent with **this receiver's reference** |
| about `f_step * d` off a real channel | an **external transmitter** |
| neither | not yet explained -- which is a finding, not a default |

## The measurement that establishes it

`testfiles/carrier_75000_bare.bin`, tuned 74 700 500 Hz, `ppm 0` in its
sidecar. `signal_find_carrier()` over the same window the sidecar's invariant
names puts the carrier at **+299 478.2 Hz**, stable to about 1 Hz across look
lengths from 65 536 to 400 000 pairs (33 ms to 200 ms):

```
pairs  65536  offset +299479.3    pairs 200000  offset +299478.3
pairs 131072  offset +299478.5    pairs 400000  offset +299478.2
```

That is an absolute **74 999 978.2 Hz -- 21.8 Hz below 25 MHz x 3**, or
-0.29 ppm. The external prediction at that tuning is **2.30 to 2.43 kHz** of
displacement for the 30.8, 31.3 and 32.5 ppm this dongle has measured
(`--lte-chain`, GSM ARFCN 113, LTE EARFCN 6200). The observation is **1% of
it**.

Ticket 10's first gate is therefore answered: whatever generates the 75 MHz
family is **clocked coherently with this receiver's reference**, so
`device_profile` is the right home for it and the different-room test is
corroboration rather than a prerequisite.

## What it says about the candidates already measured

The confirmation pass's frequency readout has to be calibrated before the
subtraction means anything, and the two known comb families do it for free --
they are internal by construction, so whatever offset they read from exact *is*
the pass's precision:

| measured | exact | reads | so |
| --- | --- | --- | --- |
| 129.600159 | 14.4 x 9 | **+159 Hz** | precision |
| 131.200526 | 1.6 x 82 | **+526 Hz** | precision |
| 136.000793 | 1.6 x 85 | **+793 Hz** | precision |

**About +/-800 Hz, or roughly one 977 Hz bin** -- and that is 5 times smaller
than the 4.1-4.6 kHz being tested for, which is the only reason the test works
at this precision. Applying it to the rest of the 128-137 MHz pass:

| measured | nearest exact | offset | verdict |
| --- | --- | --- | --- |
| 150.0009 | 150.000000 | **+900 Hz** | inside the precision -> **the receiver's reference** |
| 134.999939 | 135.000000 (on both rasters) | **-61 Hz** | **the receiver's reference**, not the AM carrier `spec.md` calls it |
| 132.062744 | 132.058333 (8.33 kHz raster) | **+4411 Hz** | vs 4066-4290 predicted -> **a real external signal** |

Two consequences land outside this ticket and both are recorded there:

- **`.scratch/am-airband/spec.md` lists 135.024 (peak 134.999939) as "a real AM
  carrier".** It reads 61 Hz from exact where a transmitter would read 4.2 kHz
  off, and its own 78% standing share and the prototype's speech-band null
  agree. Four of the airband's six strongest signals are the receiver, not
  three, and **one** external carrier survives, not two.
- **That survivor is only explicable on the 8.33 kHz raster.** The nearest
  25 kHz channels are 12.3 and 12.7 kHz away, three times the displacement, so
  `.scratch/am-airband/issues/02-*`'s plan to refuse 8.33 kHz would refuse the
  only traffic measured at this site.

## What this ticket must not claim

**The fundamental.** 25 MHz is not 28 800 000 / n, so how a 28.8 MHz reference
comes to produce a coherent family at 75.000000 and 150.000000 is unexplained;
a fractional-N synthesiser would do it, and so would the family being something
other than 25 MHz x n that happens to hit both. The measurement says *coherent
with this receiver's reference*, which is what "belongs to the receiver" has to
mean operationally, and it says nothing about which oscillator or which divider.

**That a calibrated receiver can do this.** The test needs a known and
**non-zero** `d`. Correct the ppm and the displacement goes to zero, the
externals snap onto their rasters -- and the internal tones move *off* exact by
`f_nom * d`, because their true frequency is still the crystal's. The
discriminator inverts rather than improving, and a receiver whose ppm has never
been measured has no discriminator at all. Both facts belong beside it wherever
it lands.

**That "off by `f_step * d`" identifies a transmitter.** It identifies a signal
whose oscillator is not this receiver's. A second receiver on the desk, or any
source with its own crystal, reads the same way.

## Where it belongs

The arithmetic is a fact about a *reading*, not about a device: it takes a
tuning, a ppm and a frequency, and returns the true frequency the reading
implies -- and the inverse. That is the same family as
`survey_comb_spacing_hz()`, which is a fact about a clock, so a small header
beside `survey_suspect.h` reading `device_profile`'s ppm and reference is the
shape, not a new `device_profile` field. `survey_suspect.h` is the consumer:
it already owns `SURVEY_SUSPECT_REFERENCE` and the "unremarked" gap ticket 10
wants closed.

One small thing in the way: `scripts/signal_report.c` prints the offset it was
**asked** for, not `carrier.offset_hz` -- the found offset, which is the whole
input to this subtraction, never reaches the report. `probe-signal` should
print both. Everything above needed a 20-line harness to get at a number the
tool already computes.

## What must be checkable

No receiver, no window, ADR-0012 satisfied by construction, because all of it
is arithmetic over three numbers:

1. **The forward model.** A tone at a known true frequency, a known tuning and
   a known ppm reports where the formula says, and a **clock-coherent** tone at
   the same tuning reports its exact nominal within the baseband term -- the
   two cases the whole discriminator rests on, and they must be asserted
   separately because they differ by which frequency carries `(1 + d)`.
2. **The inversion is bounded.** At `d = 0` the two cases are identical, so the
   unit must refuse rather than return a verdict -- the "a calibrated receiver
   cannot do this" refusal above, in code.
3. **The property ticket 10 asked for.** A frequency belonging to no modelled
   comb *and* no channel raster is reported as unexplained rather than clean,
   so "unremarked" stops carrying two meanings.

Then `carrier_75000_bare.bin` corroborates it on a real signal at 21.8 Hz,
which is what makes the synthetic cases worth anything.
