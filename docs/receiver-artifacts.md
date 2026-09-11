# Receiver artifacts and flag classification

Why a survey candidate might have been made by the receiver rather than
received by it, and why one might be nothing at all. Every algorithm, every
formula, every adjustable parameter, and a worked example of each.

Source: `src/survey_suspect.h` (the frequency and width tests),
`src/survey_confirm.h` (the emptiness test), `src/signal_probe.h` (the
measurements behind it). Checked by `check-suspect`, `check-survey-confirm`
and `check-signal-probe`, none of which needs a window or a receiver
(ADR-0012).

**Two words, both from `CONTEXT.md`.** A **receiver artifact** is a candidate
produced inside the receiver rather than received by it. A **reference comb**
is the regularly spaced set of tones its clock leaves across the band -- a
comb in the ordinary sense, as in a comb generator or an optical frequency
comb: spectral lines at a constant interval. The glossary lists *spur*,
*birdie*, *ghost* and *interference* under _Avoid_ for the first, and this
document was called `spur-detection.md` until somebody read its own quotation
of the vocabulary rule.

**Nothing here removes a candidate.** A flag says a frequency has the
signature of an artifact; it never says a peak *is* one. Removing peaks would
hide a real transmitter that happens to sit on a harmonic, which is the silent
editing ADR-0015 refuses. The headless report says so in as many words:
`# suspicious candidates resemble the receiver rather than the band; nothing
has been removed`.

## The seven flags

`enum survey_suspicion` in `src/survey_suspect.h`:

| flag | bit | means | set by |
| --- | --- | --- | --- |
| `SURVEY_SUSPECT_REFERENCE` | 1 | on the receiver's reference comb | the sweep, and the confirmation pass |
| `SURVEY_SUSPECT_STEP_CENTRE` | 2 | where a sweep step was tuned, so the receiver's DC offset lands there | the sweep only |
| `SURVEY_SUSPECT_UNRESOLVED` | 4 | narrower than this sweep can resolve: an observation, not a suspicion | either |
| `SURVEY_SUSPECT_NO_CARRIER` | 8 | a closer look found a prominence and nothing else | the confirmation pass only |
| `SURVEY_SUSPECT_CLOCK_COHERENT` | 16 | reads at its exact nominal, where an external signal could not | either, given a **measured** crystal |
| `SURVEY_SUSPECT_UNEXPLAINED` | 32 | a bare carrier on no modelled grid, at a frequency where the question could be asked | either, given a measured crystal |
| `SURVEY_SUSPECT_DISPLACED` | 64 | reads displaced by this receiver's own error: its oscillator is not this one | either, given a measured crystal |

The last two are section 5 below, and they are a different kind of evidence
from everything above them: the comb argues from coincidence, they argue from
cancellation.

Two predicates read them, and they are deliberately separate because a reader
acts differently on each:

- `survey_suspect_warns()` — REFERENCE, STEP_CENTRE or CLOCK_COHERENT. *Unplug
  the antenna and sweep again.* Marked `*` in the candidate list and drawn as a
  **cross** on the chart.
- `survey_suspect_contested()` — DISPLACED *and* one of those. *The comb says
  the receiver and the reading says otherwise.* Marked `*!` and drawn as a
  **cross with a dot in it**, counted separately in the caption. DISPLACED is
  deliberately **not** a warning: it says a real signal is here, which is the
  opposite of what the others say.
- `survey_suspect_empty()` — NO_CARRIER. *The frequency is empty however often
  it was seen.* Marked `~` and drawn as a **hollow dot**.

`sdrgui_survey_peak_mark()` resolves a candidate carrying both: **empty wins**,
because "there is nothing here" is what a reader acts on.

## 1. The reference comb

### What it is

A sweep of 470–690 MHz turns up a dozen narrow carriers standing 20 dB above
the floor at exact multiples of 14.4 MHz — half the RTL2832U's 28.8 MHz
reference clock. They sit inside the UHF television allocation, so the band
plan dutifully labels them "UHF television".

Unplugging the antenna sorts them into two kinds, and the distinction matters
before trusting the obvious test. Three — 489.6, 547.2 and 604.8 MHz,
harmonics 34, 38 and 42 — stay exactly where they were, within half a decibel,
run after run: made and heard entirely inside the receiver. The other nine go
with the antenna, because the dongle radiates its clock and hears itself come
back. Both are the receiver's doing, but *"unplug it and an artifact stays"*
holds only for the first kind.

### The algorithm

`survey_comb_harmonic(hz, spacing_hz, tolerance_hz)`:

```
   if tolerance_hz > spacing_hz × RECEIVER_COMB_MAX_FRACTION → 0   (refuse)
   N = floor(hz / spacing_hz + 0.5)                                (nearest)
   if N < 1                                → 0
   if |hz − N × spacing_hz| > tolerance_hz → 0
   otherwise                               → N
```

Returns the harmonic number, or 0 for none. It is a *refusal*, not a guess,
when the tolerance is too loose to mean anything — see the chance rate below.

### The two combs

```
   RECEIVER_REFERENCE_HZ   = 28 800 000        the crystal
   RECEIVER_COMB_SPACING_HZ        = 28.8 MHz / 2  = 14.4 MHz      the coarse comb
   RECEIVER_FINE_COMB_SPACING_HZ   = 28.8 MHz / 18 =  1.6 MHz      the fine comb
```

14.4 MHz is every ninth tone of the 1.6 MHz comb. Three independent things
establish the finer one:

1. **The sweep is not making them.** Sweeping the same air with the step grid
   deliberately moved — lower edges of 240.0, 239.2 and 239.5 MHz, so the step
   boundaries fall in three different places — puts the candidates at the same
   absolute frequencies every time, on step boundaries in one and step centres
   in the next.
2. **They track the crystal, not the air.** With `--ppm 0` they land on exact
   multiples, within 2.5 kHz of a 3.7 kHz bin. With this site's +35 ppm
   correction applied they read about +35 ppm high — a spur divided down from
   the crystal that clocks both tuner and ADC keeps its ratio to the nominal
   grid, and a transmitter moves the other way.
3. **Every ninth tone is already established.** 244.8 = 17 × 14.4 and
   259.2 = 18 × 14.4 are on the comb the unplug test settled.

Across the whole-tuner sweep of 2026-09-03, **43% of 289 candidates** sit
within half a bin of a 1.6 MHz multiple, against **13% by chance**. On
240–270 MHz it is 11 of 14, of which the coarse test alone flagged the two
that are also multiples of 14.4.

### The chance rate, and the parameter that bounds it

A real signal lands within `tolerance` of a multiple by coincidence at a rate
of

```
   P(chance) = 2 × tolerance_hz / spacing_hz
```

Worked, at the 25 kHz tolerance below:

| comb | spacing | P(chance) | one candidate in |
| --- | --- | --- | --- |
| coarse | 14.4 MHz | 2 × 25 k / 14.4 M = **0.35%** | 288 |
| fine | 1.6 MHz | 2 × 25 k / 1.6 M = **3.1%** | 32 |

And at a full-tuner sweep's half-bin of 106 kHz:

| comb | P(chance) |
| --- | --- |
| coarse | 2 × 106 k / 14.4 M = **1.5%** |
| fine | 2 × 106 k / 1.6 M = **13%** — one in eight |

**`RECEIVER_COMB_MAX_FRACTION = 1/40`** is what refuses the last row. A comb
test may only run when the sweep can place a candidate to a fortieth of the
comb's spacing:

```
   usable when   tolerance_hz ≤ spacing_hz / 40
   coarse:       tolerance ≤ 360 kHz     — always satisfied in practice
   fine:         tolerance ≤  40 kHz     — a sweep no wider than about 650 MHz
```

Wider than that, `survey_comb_harmonic()` returns 0 and the fine comb says
**nothing** rather than something one time in eight. This is the parameter to
be most careful with: loosening it does not produce more flags, it produces
flags that mean nothing.

### The tolerance

**`RECEIVER_COMB_TOLERANCE_HZ = 25 000`.**

Half a survey bin is the obvious answer and it is not enough. A candidate is
reported at the bin holding the peak-held maximum, and noise plus the tuner's
own error can pull that a bin or two off centre. Measured:

| tone | sweep | reported error | half-bin |
| --- | --- | --- | --- |
| 648 MHz | 470–690 MHz | 18.1 kHz low | 13.4 kHz |
| 648 MHz | 470–690 MHz, next run | 11.5 kHz high | 13.4 kHz |
| 590.4 MHz | 80 MHz range | 5.3 kHz high | 4.9 kHz |

Tying the tolerance to the bin missed all three. 25 kHz covers what has been
measured with margin and costs 0.35% of candidates to coincidence.

The effective tolerance is the larger of that and the quantisation:

```
   survey_suspect_tolerance(plan, rate, fft) = max(bin_hz / 2,
                                                   4 × rate / fft / 2)
   survey_comb_tolerance(...)                = max(that, 25 kHz)
```

### Worked example

A candidate at **302.4011 MHz**, from a 290–310 MHz sweep with 8192 bins:

```
   bin_hz    = 20 MHz / 8192            = 2441 Hz
   half-bin  = 1221 Hz
   fine res  = 4 × 2e6 / 2048 / 2       = 1953 Hz
   tolerance = max(1221, 1953)          = 1953 Hz
   comb tol  = max(1953, 25 000)        = 25 000 Hz

   coarse: usable? 25 000 ≤ 14.4 M / 40 = 360 000       yes
           N = floor(302.4011/14.4 + 0.5) = 21
           21 × 14.4 MHz = 302.4 MHz
           |302.4011 − 302.4| = 1.1 kHz ≤ 25 kHz        FLAG
```

302.4 MHz is exactly 21 × 14.4 MHz. The same sweep also flagged
**296.0010 MHz**: not a coarse harmonic (296/14.4 = 20.56) but
296.0 = 185 × 1.6 MHz exactly, caught by the fine comb — which was usable
because 25 kHz ≤ 1.6 M / 40 = 40 kHz.

## 2. Narrowness, and why the fine comb needs it

### The algorithm

A pure carrier has no bandwidth of its own; what a transform measures is the
window's response, about four bins of it at the −20 dB point.

```
   RECEIVER_TONE_BINS = 4

   survey_tone_width_hz(bin_hz, rate, fft) = 4 × max(bin_hz, rate / fft)
   survey_is_unresolved(bw, ...)           = bw ≤ tone_width × 1.25
```

The `max` is load-bearing. A swept survey's bins are usually coarser than the
transform's — 3.7 kHz against 977 Hz on a 30 MHz range — and a candidate's
width comes out of the *survey* array, so it is quantised to survey bins.
Judging it against the transform's resolution calls every tone in a swept
survey "resolved", which is how the narrowness observation came to be
unavailable in exactly the case that needs it. The 1.25 is slack: the
measurement is itself a few bins wide.

### Why the fine comb requires it

1.6 MHz is **sixteen times** the 100 kHz raster broadcast services sit on, so
one FM channel in sixteen falls on the comb exactly — including 94.4 MHz, the
loudest station at this site, confirmed at 46 dB. Flagged candidates are set
aside from a report's per-allocation bests, so a frequency test alone would
hide a real transmitter: worse than the fault it fixes.

The width settles it, and the gap is not close:

| sweep | what the candidates measure |
| --- | --- |
| 240–270 MHz, comb tones | one or two survey bins |
| 88–108 MHz, broadcast stations | twenty-three to seventy-four bins |

The one narrow candidate in band II is **102.4 MHz = 64 × 1.6**, six bins
wide: a comb tone sitting in the broadcast band.

So:

```
   REFERENCE is set by   coarse harmonic alone
                    or   (fine harmonic AND unresolved)
```

### A separate question: could the sweep have resolved anything?

`survey_extent_is_floor(extent_hz, bin_hz)` with
**`SURVEY_RESOLVED_BINS = 2.5`**:

```
   at the floor when   extent_hz ≤ bin_hz × 2.5
```

A full-tuner sweep is 1742 MHz in 8192 bins, so a bin is 212 kHz and a 25 kHz
carrier occupies a fraction of one — its extent comes back as one or two bins
whatever it really is. The *same* one-or-two-bin extent from a 27 MHz sweep,
where a bin is 3.3 kHz, is a genuine measurement of something narrow. Two and
a half bins rather than two, because a maximum sitting between two bins
occupies both.

This is what the saved file's `resolved` field carries, and what makes a
reported width readable at all.

## 3. Step centres

Where a sweep step was tuned is where the receiver's own DC offset lands.

```
   survey_at_step_centre(plan, hz, tolerance):
       step = floor((hz − plan.lower_hz) / plan.step_span_hz)
       for s in {step−1, step, step+1}:
           if |hz − survey_plan_step_centre(plan, s)| ≤ tolerance → 1
       otherwise → 0
```

Three steps are searched because the arithmetic can land either side of a
boundary. The tolerance is the quantisation one — a step centre is an *exact*
frequency the receiver was told to tune to, so it needs no extra room.

**Only tested when the DC-spike filter is off.** With it on there is no offset
to land there, and step centres are 1.6 MHz apart, so an unconditional test
would flag every DVB-T channel centre in the band — 8 MHz is exactly five
steps. Even when set, something real may be underneath: what the flag says is
that the measurement at this frequency has the receiver's offset added to it.

The confirmation pass leaves this test out entirely
(`survey_suspect_confirmed()` passes `dc_filtered = 1`): a confirmation look
is tuned *to* the candidate, so every candidate is at its centre and the test
would flag all of them.

## 4. Nothing there at all

The newest flag, and the only one that needs a measurement rather than
arithmetic on a frequency.

### What it is for

Some frequencies clear the confirmation pass's bar and are still empty. Five
in one 290–310 MHz sweep were confirmed **six looks out of six**:

The standing shares in this table were recorded before the statistic became a
mean over segments, so they read lower than the same signals would now; the
envelope is what the flag rests on and is untouched.

| frequency | prominence | over floor | standing share | envelope |
| --- | --- | --- | --- | --- |
| 292.9480 MHz | 12.0 dB | 11.9 | 0.006 | 0.533 |
| 307.3560 MHz | 12.6 dB | 11.9 | 0.007 | 0.537 |
| 303.1018 MHz | 11.1 dB | 10.8 | 0.008 | 0.525 |
| 308.9612 MHz | 13.8 dB | 11.3 | 0.005 | 0.520 |
| 300.6519 MHz | 12.5 dB | 11.0 | 0.005 | 0.539 |

A prominence bar is cleared by noise structure every time it is offered, so
counting looks cannot separate them and more looks cannot help. Raising the
bar is the wrong direction and `survey_confirm.h` argues it out: it "teaches
the history that a real transmitter is noise". Two independent statistics can.

### The algorithm

`survey_confirm_is_empty(measured, carrier, envelope)`:

```
   requires   measured                              (the pass caught it)
   requires   envelope.found
   requires   signal_carrier_verdict(carrier) == SIGNAL_NOTHING
   requires   |envelope.variation − 0.5227| ≤ 0.10
```

**Both must hold.** Either alone gets a case wrong the other catches, and both
failures are on record:

- A **pulsed** transmission has real energy and no standing carrier — Mode S
  reads −2.0 dB — so the carrier test alone calls it empty.
- A **weak signal buried at its own noise floor** reads Rayleigh, so the
  envelope test alone calls it empty too.

### Rayleigh, and why it is the reference

A complex Gaussian's magnitude is Rayleigh distributed, whose coefficient of
variation is

```
   CV = sqrt(4/π − 1) = 0.5227…
```

It depends on **nothing** — not level, not gain, not bandwidth. That is what
makes it usable as a reference and what makes five independent frequencies
landing on it evidence rather than coincidence.

**`SIGNAL_ENVELOPE_RAYLEIGH = 0.5227`**,
**`SURVEY_NOISE_ENVELOPE_TOLERANCE = 0.10`**, measured from both sides:

| signal | variation | distance from Rayleigh |
| --- | --- | --- |
| the five noise readings | 0.520–0.539 | ≤ **0.017** |
| a GSM carrier (nearest signal) | 0.792 | 0.27 |
| TETRA, π/4-DQPSK | 0.252–0.272 | 0.25–0.27 |
| a bare carrier | 0.137–0.248 | 0.27–0.39 |
| FM broadcast | 0.032 | 0.49 |
| Mode S, pulsed | 1.057 | 0.53 |
| an LTE downlink, OFDM | 1.098 | 0.58 |

A tenth is five times the noise spread away from the noise and less than a
third of the way to the nearest signal.

### The carrier test

`signal_carrier_verdict()` in `src/signal_probe.h`:

```
   SIGNAL_NOTHING    when carrier_over_noise_db < SIGNAL_CARRIER_PRESENT_DB
   SIGNAL_BARE       when carrier_power_fraction ≥ SIGNAL_BARE_FRACTION
   SIGNAL_MODULATED  otherwise
```

**`SIGNAL_CARRIER_PRESENT_DB = 15`.** A search over thousands of frequencies
takes the largest of thousands of noise samples, so pure noise reliably
produces a "line": ten draws gave **8.2 to 13.6 dB** over the median floor.
Fifteen sits above every one of those, against 49.8 dB for the bare carrier at
75.000 MHz.

**`SIGNAL_BARE_FRACTION = 0.80`,** and the table behind it was re-measured
when `carrier_power_fraction` became a mean over segments. Over the whole of
each capture: a synthetic tone 1.00, the 75.0005 MHz recording **0.923**, then
Mode S 0.327, FM 0.145, TETRA 0.100, GSM 0.063, LTE 0.028. Every negative rose
— inside one segment a modulated signal is partly coherent — and 0.80 still
sits **0.12 under the lowest positive and 0.47 above the highest negative**,
with nothing in the corpus between 0.33 and 0.92.

> **The defect this replaced.** `carrier_power_fraction` used to fall as the
> observation lengthened, because it mixed at one fixed frequency and a
> drifting carrier walks out of phase — the 75 MHz harmonic read 0.888–0.921
> from 0.07 s to 1 s and **0.779 at 2 s**, crossing the threshold and turning
> a bare carrier into a modulated one. It is a mean over segments of
> `SIGNAL_STANDING_SEGMENT_BLOCKS` now and reads 0.905 at one block against
> 0.923 at 2 s. No shipped verdict changed: both callers pass one block, and
> every verdict in the corpus is the same at that length.
> `.scratch/standing-fraction-drifts/` has the segment-length measurement and
> the reason the per-segment fractions are averaged rather than summed.

### What the flag claims

**"Indistinguishable from noise", not "is noise".** A real spread signal buried
at its own noise floor reads the same, and nothing here can tell those apart —
which is why the words on screen say what was measured (ADR-0015).

### The consequence, and it is the only one

A frequency flagged `no-carrier` is **barred from the receiving setup's
history** (ADR-0022), checked *before* the verdict rather than after because it
overrides all three -- an amendment recorded in ADR-0019, which otherwise
admits an intermittent new carrier: a
confirmed empty frequency is still empty and an intermittent one is noise that
came and went. Without that, five noise readings enter the site's memory as
signals, are remembered for ever, and are reported "gone" whenever a later
sweep fails to find the same noise.

### One case it does not catch, deliberately

**299.4262 MHz** read an envelope of 0.547 with 1.4% of the channel standing
still — noise on both counts — and **15.8 dB** over its floor, 0.8 dB above
`SIGNAL_CARRIER_PRESENT_DB`. The carrier test passes it and the flag stays
silent. The threshold was measured; the two flagged in the same sweep read 9.5
and 10.9 dB. Loosening it to catch one case is tuning a constant until it
agrees.

## 5. Where it reads, which is the other kind of evidence

### What it is

Everything above argues from coincidence: this frequency is a multiple of that
spacing, and a real signal would land there rarely. This argues from
cancellation, shares no arithmetic with any of it, and therefore corroborates
or **contradicts** it.

An uncalibrated receiver does not report a frequency vaguely, it reports it
*wrongly by a known amount*. Write `k` for the reference's own fractional error
and `c` for the correction in force, so the residual is `e = k − c`. A tone at
true frequency `f` is reported at `f / (1 + e)` — the tuning cancels out of
that exactly, because one crystal clocks both the synthesiser and the ADC.

Now put a tone through it that is *generated from the same reference*. Its true
frequency is `f_nom · (1 + k)`, so it is reported at `f_nom · (1 + k) / (1 + e)`.
Uncorrected (`c = 0`) that is **exactly its nominal, however far out the
crystal is.** That is the whole discriminator, and it is one subtraction.

The two readings are `f · k / (1 + e)` apart — **`f · k`, the crystal's own
error, and the correction does not enter it at all.** This dongle measures
31.84 ppm, so the hypotheses sit **4.1 kHz apart at 132 MHz and 4.8 at 150**.

### The sign, which ticket 11 had backwards

`cal-measure` prints `observed_ppm`, the *residual* `(measured − expected) /
expected`, and it reads **−31.84** here (`--calibrate gsm --arfcn 113`, 874
measurements, sem 0.22, `suggested_ppm 32`). That is not `k`, it is its
negation, because reported frequency is `f / (1 + e)`. **This crystal is
fast**, so an uncorrected reading of a real transmitter comes back about
4.2 kHz **low** at 132 MHz, not high.

Every "reads exact, therefore clocked here" verdict survives untouched — being
*at* the nominal cannot care which way the other hypothesis lies — so the table
below keeps its verdicts. What moves is the one external attribution: the
airband survivor is on **132.066667** and not the 132.058333 the ticket names,
which the corrected sign puts 8.6 kHz away instead of 282 Hz.

### What it settled

| measured | nearest exact | offset | verdict |
| --- | --- | --- | --- |
| 129.600159 | 14.4 × 9 | +159 Hz | the receiver — and this *is* the precision figure |
| 131.200526 | 1.6 × 82 | +526 Hz | the receiver |
| 136.000793 | 1.6 × 85 | +793 Hz | the receiver |
| 150.000900 | 150.000000 | +900 Hz | the receiver's reference, **on no modelled comb** |
| 134.999939 | 135.000000 | −61 Hz | the receiver, not the AM carrier it was recorded as |
| 132.062744 | 132.066667 (8.33 kHz raster) | −3923 Hz | external, against −4204 predicted: 282 Hz |

The first three are internal by construction, so what they read from exact *is*
the measurement's precision: about one 977 Hz bin. That is four times smaller
than the separation being tested for, which is the only reason the test works
at the resolution available.

### On air, with the correction applied

The table above is an *uncorrected* receiver, where a coherent tone reads on
its exact multiple. Turn the correction on and the model says every one of them
must move up by `f · k` — about 4.2 kHz — while nothing on air changes. A
128–137 MHz sweep on 2026-09-11 with `calibration … 32 …` in force, binning at
1098.6 Hz:

| read | nominal | coherent predicts | error | flagged |
| --- | --- | --- | --- | --- |
| 131.204163 | 1.6 × 82 | 131.204198 | **−35 Hz** | `clocked-here` |
| 129.604553 | 14.4 × 9 | 129.604147 | +406 Hz | `reference,clocked-here` |
| 136.005188 | 1.6 × 85 | 136.004352 | +836 Hz | `clocked-here` |
| 128.006042 | 1.6 × 80 | 128.004096 | +1946 Hz | — (outside one bin) |
| 135.005432 | no comb | — | — | — |

Three of three known families predicted to within a bin, having each moved
four kilohertz from where the uncorrected pass found them. The fourth is
honestly missed rather than quietly admitted, which is what a tolerance of one
bin buys.

### The algorithm

`src/reading_origin.h`, and it is pure arithmetic over three numbers.

1. **Refuse unless separable.** The coherent answer sits at `nominal ±
   tolerance` and the external one at `nominal + displacement ± tolerance`.
   Those windows are disjoint exactly when the displacement exceeds **twice**
   the tolerance, so that is the bar — the honest minimum, not a chosen one.
2. **Ask against a nominal the caller proposes** — a comb multiple, a channel
   on a raster. Within tolerance of it: `RECEIVER`. Within tolerance of the
   displaced position: `EXTERNAL`. Neither: `UNEXPLAINED`.
3. `survey_suspect_origin()` asks over the two combs and maps the answer onto
   the two flags.

### The three refusals, and one of them is backwards

`struct reading_clock` carries **two** numbers, `crystal_ppm` and
`applied_ppm`, and a crystal error of zero is a refusal rather than a good
receiver. Two situations produce it:

- a receiver nobody has ever calibrated — the error is unknown, so nothing can
  be concluded from where anything reads;
- a capture — a file does not carry its recorder's crystal, the same reason a
  capture gets no comb tests at all.

**A calibrated receiver is not one of them**, and the first version of this
took it to be. It took one number, `calibrated − applied`, on the reasoning
that a corrected receiver has nothing left to displace anything by. But the
program restores a stored calibration at startup and applies it, so that
difference is zero on every calibrated receiver and zero again on every
uncalibrated one: **the flag could never be set in the shipping program**,
while the whole unit suite stayed green because a unit hands the number in.

What correcting the ppm actually does is **invert** the discriminator, not
remove it. The separation stays `f · k`. What changes is which hypothesis sits
on the nominal: uncorrected the coherent tone reads exactly `N` and the
external one is displaced; corrected the external one reads exactly `N` and
the coherent one is displaced by `f_nom · k` — 4.8 kHz at 150 MHz, looking
exactly like the external signal it is not. That is why the correction is an
input rather than an assumption.

### The tolerance is one bin, and not the comb's

`SURVEY_COHERENT_BINS` is 1.0 — one bin of whatever measured the candidate.
Borrowing `RECEIVER_COMB_TOLERANCE_HZ`'s 25 kHz looks like tidying and is not:
it would not loosen this test, it would **abolish** it. Twice 25 kHz of
required displacement wants a carrier at 1.6 GHz, every verdict would be
"cannot say", and the suite would look perfectly healthy. 25 kHz is cheap
against a 14.4 MHz comb spacing and ruinous against a 4 kHz subtraction.

It follows that most swept surveys cannot ask this at all — a whole-tuner
sweep bins at 212 kHz and would need 424 kHz of displacement — while a
confirmation pass, tuned to the candidate at the receiver's own rate, bins at
977 Hz and can. The flag appears where the evidence is.

### The contradiction, and the two resolutions it refuses

`SURVEY_SUSPECT_DISPLACED` is the answer that disagrees with a comb mark, and
it is the reason a second kind of evidence was worth having at all. 94.4 MHz is
1.6 × 59 and is also the loudest FM broadcast station at this site: the fine
comb flags it, correctly by its own lights and wrongly about the world, and a
real transmitter there reads about 3.0 kHz off exact.

**It does not clear `SURVEY_SUSPECT_REFERENCE`.** This file never removes a
candidate and never says a peak *is* an artifact; clearing a mark an operator
has learned to read is a larger act than adding one beside it, and stronger
evidence is not the same as evidence entitled to overrule silently.

The chart draws **one** mark per peak, so "beside" had to become a fourth
shape. Both obvious resolutions are wrong: a plain cross tells a reader to stop
looking at the one candidate they should look at, and a plain dot silently
discards the comb mark. `SDRGUI_PEAK_CONTESTED` carries both.

That separation needs a confirmation pass's 977 Hz bin. A band II sweep binning
at 2 kHz cannot separate the two hypotheses at 94 MHz and says so, which is the
refusal working — the separation grows with frequency and a bin does not, so
the airband candidates forty megahertz higher are reachable from a sweep where
this one is not.

The accepted cost is recorded rather than hidden: such a candidate **still
counts as suspicious**, so on band II — where one channel in sixteen falls on
the fine comb — a caption can say "mostly the receiver" about a band where the
program has positive evidence the loudest thing is a station. The caption
prints the contested count separately for that reason. If it misleads in
practice the fix is to report both numbers, not to start clearing marks.

### A service raster answers one hypothesis, not two

A channel grid says where a **transmitter** may sit, so it may test the
external hypothesis and not the coherent one. "A tone clocked by this receiver
that happens to land on an airband channel" is not a hypothesis anybody holds,
and the arithmetic makes it worse than useless: at the airband's 8333 Hz
spacing and a pass's 977 Hz tolerance a reading lands within tolerance of
*some* channel **23% of the time**, against 0.12% for the 1.6 MHz comb. That
is `RECEIVER_COMB_MAX_FRACTION`'s argument with different numbers.

It was found on air, not here. A 128–152 MHz sweep produced
`confirm 134758789 new refuted 2.2 0/6 977 unresolved,clocked-here` — a noise
maximum found in none of six looks, called a tone clocked by this receiver,
because its measured centre landed 433 Hz from where a coherent source on
airband channel 1990 would read.

### What must be a carrier before it can be unexplained

A noise maximum is narrow, so it carries `UNRESOLVED` exactly as a tone does.
The first live sweep with `SURVEY_SUSPECT_UNEXPLAINED` in it turned five
refuted peaks at 1.9–4.4 dB into "unexplained bare carriers" — a false warning
in the one direction this whole file says not to take, and one no unit check
was asking about. Two gates were added from it: the flag wants the
confirmation pass's own `NO_CARRIER` to be *absent*, and a target the pass
**refuted** has the flag cleared outright, because a frequency found in none of
six looks is absent rather than unexplained. `clocked-here` is kept whatever
the verdict — it says where a frequency read, and something seen once still
read somewhere.

### What it must not claim

**Which oscillator.** 25 MHz is not 28.8/n, so how a 28.8 MHz reference comes
to produce a coherent family at 75.000000 and 150.000000 is unexplained. The
measurement says *coherent with this receiver's reference*, which is what
"belongs to the receiver" can mean operationally, and nothing about which
divider.

**That "displaced" means a transmitter.** It means an oscillator that is not
this one. A second receiver on the desk, a powered hub, a monitor — anything
with its own crystal reads the same way.

## 6. A clock family in octaves

`src/clock_chain.h`. The reference comb is "a tone every reference/n", which a
divider leaves across the band. This is a different shape: **f, 2f, 4f and
never 3f**, which is what a doubler or divider chain produces and what harmonic
distortion of one oscillator does not.

Seven 2 MHz windows at `--ppm 0`, a 976.6 Hz bin, an 8 dB bar, 2026-09-11:

| tested | found | prominence |
| --- | --- | --- |
| **75.000000** | +488 Hz | 17.0 dB |
| **150.000000** | +488 Hz | 11.2 dB |
| **300.000000** | +488 Hz | 17.0 dB |
| 37.5, 175, 225, 600 | absent, nearest 61–369 kHz away | — |

All three present members read **+488 Hz, exactly half a bin** — the
quantisation of a tone at its bin's centre, identical at all three, so it is
the grid and not the sources. An external source would have read 2.4, 4.8 and
9.6 kHz low.

**225 MHz is the finding.** It is 75 × 3 and absent at a 12 dB bar while ×1,
×2 and ×4 stand at 11–17 dB. A harmonic model would flag it; an octave model
does not, and `clock_chain_is_off_octave()` exists so that difference is a
checked property rather than a remark.

The fundamental is a **parameter**, and `CLOCK_CHAIN_MEASURED_FUNDAMENTAL_HZ`
is named as a site measurement rather than a device constant. Nothing went into
`device_profile`: what is confirmed is a family on this receiver at this site,
and a profile field would assert it of the part
(`.scratch/device-model/issues/10-*`).

On air, with the correction in force:
`confirm 150123535 new confirmed 12.5 5/5 ... clocked-here 150005346` against a
predicted 150.004800 — **+546 Hz**, inside a bin, on a frequency no comb
reaches.

**And a second family is established but has no rule.** Measured 2026-09-12:
135.000000 and 540.000000 are clock-coherent — +75 Hz and −190 Hz against the
corrected prediction — and on neither comb nor octave chain. They are not an
octave chain of their own either, because **270 = 135 × 2 is absent**, and a
narrow sweep with a confirmation pass calls the one candidate near it
`no-carrier`.

So the coherent set is **75, 135, 150, 300, 540** and the absences are
**37.5, 175, 225, 270, 600**. As multiples of 15 MHz that is 5, 9, 10, 20, 36
present against 15, 18, 40 absent, which no single multiplicative rule
produces. A ×4 step would do it, and so would a doubler whose intermediate is
not radiated — ordinary, and unfalsifiable from outside the box.

`clock_chain.h` is therefore **incomplete rather than wrong**: it covers 75,
150 and 300, claims nothing about 270, and flags nothing falsely. 135 and 540
read `unexplained`, which is what that flag is for — asked, and nothing
modelled accounts for it.

## Every adjustable parameter

Compiled in today. `.scratch/calibrating-the-flags/` is the open effort to
measure them per device and reach them from Settings.

| parameter | value | file | what constrains it |
| --- | --- | --- | --- |
| `RECEIVER_REFERENCE_HZ` | 28.8 MHz | `survey_suspect.h` | the RTL2832U's specified crystal on **this** dongle |
| `RECEIVER_COMB_SPACING_HZ` | 14.4 MHz | derived, /2 | measured on a disconnected sweep |
| `RECEIVER_FINE_COMB_SPACING_HZ` | 1.6 MHz | derived, /18 | three independent arguments above |
| `RECEIVER_COMB_TOLERANCE_HZ` | 25 kHz | `survey_suspect.h` | covers three measured reporting errors of 5.3–18.1 kHz; costs 0.35% to chance |
| `RECEIVER_COMB_MAX_FRACTION` | 1/40 | `survey_suspect.h` | past it a flag is chance: at 1.6 MHz a 106 kHz tolerance is 13% |
| `RECEIVER_TONE_BINS` | 4 | `survey_suspect.h` | the window's own −20 dB response |
| `SURVEY_RESOLVED_BINS` | 2.5 | `survey_suspect.h` | a maximum between two bins occupies both |
| `SIGNAL_CARRIER_PRESENT_DB` | 15 dB | `signal_probe.h` | noise reaches 8.2–13.6 dB over its own median |
| `SIGNAL_BARE_FRACTION` | 0.80 | `signal_probe.h` | tone 1.00, the harmonic 0.923, highest modulated 0.327 |
| `SIGNAL_STANDING_SEGMENT_BLOCKS` | 64 | `signal_probe.h` | 64 keeps a bare carrier over 0.98 out to 10 Hz/s of drift; 1/64 is the noise bias it costs |
| `SIGNAL_ENVELOPE_RAYLEIGH` | 0.5227 | `signal_probe.h` | `sqrt(4/π − 1)`; not adjustable, it is a constant of the distribution |
| `SURVEY_NOISE_ENVELOPE_TOLERANCE` | 0.10 | `survey_confirm.h` | noise within 0.017, nearest signal 0.27 |
| `SURVEY_MIN_PROMINENCE_DB` | 8 dB | `survey_sweep.h` | ADR-0017; three replacements built, measured on air, put back |
| `SURVEY_CONFIRM_PROMINENCE_DB` | 6 dB | `survey_confirm.h` | under the sweep's own bar, because refuting a real signal is the expensive error |
| `SURVEY_CONFIRM_LOOKS` | 6 | `survey_confirm.h` | enough that one burst in six is distinguishable from five |
| `SURVEY_COHERENT_BINS` | 1.0 | `survey_suspect.h` | the pass's measured precision: three comb tones at +159, +526, +793 Hz through a 977 Hz bin |
| `READING_SEPARABLE_TOLERANCES` | 2.0 | `reading_origin.h` | not adjustable: two windows of half-width `t` are disjoint exactly past `2t` |
| `CLOCK_CHAIN_MEASURED_FUNDAMENTAL_HZ` | 75 MHz | `clock_chain.h` | measured on **one receiver at one site**: ×1, ×2, ×4 present at 11–17 dB, ×3 absent at 12 dB |
| `CLOCK_CHAIN_MAX_OCTAVES` | 8 | `clock_chain.h` | one step past the highest member found; the bound exists so an absurd fundamental terminates |

**Three of them are not free parameters.** `SIGNAL_ENVELOPE_RAYLEIGH` is
`sqrt(4/π − 1)` and changing it means comparing against something that is not
noise. `READING_SEPARABLE_TOLERANCES` is the condition for two intervals to be
disjoint and is arithmetic, not a threshold. `RECEIVER_COMB_MAX_FRACTION`
bounds every comb tolerance, and loosening it does not produce more flags — it
produces flags with no evidence behind them.

And `SURVEY_COHERENT_BINS` is adjustable but must not be raised to the comb's
tolerance, for the reason section 5 gives: past about twice its present value
the test stops answering rather than starting to over-answer, which is the
failure mode a green suite cannot show.

## What is not established

The comb spacing here was measured. **Whether another receiver's differs is
not.** 28.8 MHz is what the RTL2832U's datasheet specifies, so it may well be
near-universal on these dongles; the assertion that 26 MHz and 24 MHz parts
ship was made in `.scratch/calibrating-the-flags/` without checking and should
not be repeated until somebody has.

That matters for how the calibration effort is motivated. If 28.8 MHz is
universal, the case is not "the constant is wrong elsewhere" but "the constant
is unverified elsewhere, and a measured one would be evidence rather than
assumption" — a smaller claim, and one that needs a second dongle before a
comb-fitting routine can be trusted. A fit that returns 14.4 MHz on the one
receiver whose answer is already hardcoded is a round trip, and
`.claude/skills/dsp-validation/` says what a round trip cannot establish.

## Reading a candidate end to end

A row from a confirmed 290–310 MHz sweep:

```
confirm 302399902 new confirmed 35.2 5/5 2930 reference,unresolved
kind 302399902 a bare carrier 55.6 0.996 0.044 level 0.0000
```

- `302399902` — 1.1 kHz from 21 × 14.4 MHz, inside the 25 kHz tolerance →
  **REFERENCE**
- `2930` Hz wide. **Judged at the confirmation pass's own resolution, not the
  sweep's**: `survey_suspect_confirmed()` builds a plan with
  `bin_hz = rate / fft = 976.6 Hz`, so the tone width is 4 × 976.6 = 3.9 kHz
  and the floor is × 1.25 = 4.9 kHz. 2930 ≤ 4883 → **UNRESOLVED**. Against the
  *sweep's* 2441 Hz bins the floor would have been 12.2 kHz, which is why the
  two resolutions must not be mixed up
- `55.6` dB over the floor beside it, well past 15 → a carrier is there
- `0.996` of the channel standing still, past 0.80 → **a bare carrier**
- `0.044` envelope variation, 0.48 from Rayleigh → not empty, so no
  **NO_CARRIER**
- `level` — no burst structure

Read together: a bare, unmodulated, narrow, continuous carrier on an exact
multiple of half the receiver's crystal. The band plan calls that frequency
"Fixed and mobile". The flags say it is the instrument, the measurements say
there is nothing riding it, and neither says the other's job.
