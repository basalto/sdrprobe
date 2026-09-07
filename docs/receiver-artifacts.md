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

## The four flags

`enum survey_suspicion` in `src/survey_suspect.h`:

| flag | bit | means | set by |
| --- | --- | --- | --- |
| `SURVEY_SUSPECT_REFERENCE` | 1 | on the receiver's reference comb | the sweep, and the confirmation pass |
| `SURVEY_SUSPECT_STEP_CENTRE` | 2 | where a sweep step was tuned, so the receiver's DC offset lands there | the sweep only |
| `SURVEY_SUSPECT_UNRESOLVED` | 4 | narrower than this sweep can resolve: an observation, not a suspicion | either |
| `SURVEY_SUSPECT_NO_CARRIER` | 8 | a closer look found a prominence and nothing else | the confirmation pass only |

Two predicates read them, and they are deliberately separate because a reader
acts differently on each:

- `survey_suspect_warns()` — REFERENCE or STEP_CENTRE. *Unplug the antenna and
  sweep again.* Marked `*` in the candidate list and drawn as a **cross** on
  the chart.
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

**`SIGNAL_BARE_FRACTION = 0.80`.** A synthetic tone in no noise reads 1.00 and
the 75.000 MHz recording 0.87, against 0.00 for a modulated 1090 MHz carrier
and 0.00 for an empty frequency.

> **Known defect.** `carrier_power_fraction` falls as the observation
> lengthens, because it mixes at one fixed frequency and a drifting carrier
> walks out of phase — the 75 MHz harmonic reads 0.888–0.921 from 0.07 s to
> 1 s and **0.779 at 2 s**, crossing the threshold and turning a bare carrier
> into a modulated one. No shipped path hits it: both callers pass one block.
> `.scratch/standing-fraction-drifts/` has the fix and the table that has to
> be re-measured with it.

### What the flag claims

**"Indistinguishable from noise", not "is noise".** A real spread signal buried
at its own noise floor reads the same, and nothing here can tell those apart —
which is why the words on screen say what was measured (ADR-0015).

### The consequence, and it is the only one

A frequency flagged `no-carrier` is **barred from the site history**, checked
*before* the verdict rather than after because it overrides all three: a
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
| `SIGNAL_BARE_FRACTION` | 0.80 | `signal_probe.h` | tone 1.00, the harmonic 0.87, modulated ~0.00 |
| `SIGNAL_ENVELOPE_RAYLEIGH` | 0.5227 | `signal_probe.h` | `sqrt(4/π − 1)`; not adjustable, it is a constant of the distribution |
| `SURVEY_NOISE_ENVELOPE_TOLERANCE` | 0.10 | `survey_confirm.h` | noise within 0.017, nearest signal 0.27 |
| `SURVEY_MIN_PROMINENCE_DB` | 8 dB | `survey_sweep.h` | ADR-0017; three replacements built, measured on air, put back |
| `SURVEY_CONFIRM_PROMINENCE_DB` | 6 dB | `survey_confirm.h` | under the sweep's own bar, because refuting a real signal is the expensive error |
| `SURVEY_CONFIRM_LOOKS` | 6 | `survey_confirm.h` | enough that one burst in six is distinguishable from five |

**Two of them are not free parameters.** `SIGNAL_ENVELOPE_RAYLEIGH` is
`sqrt(4/π − 1)` and changing it means comparing against something that is not
noise. `RECEIVER_COMB_MAX_FRACTION` bounds every comb tolerance, and loosening
it does not produce more flags — it produces flags with no evidence behind
them.

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
