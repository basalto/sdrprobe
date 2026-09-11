# 10 - There is a second clock here, and the comb test cannot see it

Status: **ready-for-agent, 2026-09-11.** All three gates are answered and the
family is measured: **75, 150 and 300 MHz, clock-coherent; 37.5, 175, 225 and
600 absent.** What remains is the representation, and the measurement now
constrains it. See the comments.
Opened 2026-09-10, from the sweep `surveys/2026-09-10-002420-24M-1766M.json`
and three narrow confirmation passes over it.

`device_profile.reference_clock_hz` is **one** number -- 28 800 000 for the
RTL2832U -- and `survey_comb_spacing_hz()` derives the receiver-like marks
from it: a tone every reference/2 (14.4 MHz) and a finer one every
reference/18 (1.6 MHz). That machinery works. In the 128-137 MHz sweep it
flagged both of the two strongest candidates, correctly:

| measured | is | flagged |
| --- | --- | --- |
| 129.600159 MHz | 14.4 x 9 | `reference` |
| 136.000793 MHz | 1.6 x 85 | `reference` |
| 131.200526 MHz (power centre) | 1.6 x 82 | `reference` |

Each reads as a bare carrier 47-48 dB over its floor with 98% of the channel
standing still. That is the comb doing exactly its job.

## What it cannot see

**150.0009 MHz**, confirmed 6 of 6 looks at 37.7 dB over noise with 70% of
the channel standing still, filed by the band plan under "Mobile-satellite
uplink", and **not flagged**. It is not a multiple of 14.4 or of 1.6.

It is within a kilohertz of **25 MHz x 6**, and that is a family this
repository already has a capture of: `testfiles/carrier_75000_bare.bin` is
75.0005 MHz, which `docs/what-is-on-air.md` records as **25 MHz x 3** after
the band plan called it an ILS marker beacon and every ILS sideband probe came
back within 3 dB of a control. Two harmonics of the same ~25 MHz source, one
already committed as a test capture, and the profile has nowhere to say so.

## Why this is triage and not ready

Three things have to be established, and the first is the one that decides the
shape of the fix.

**Whose 25 MHz is it.** A comb belongs to the receiver only if it moves with
the receiver. Everything in `docs/receiver-artifacts.md` assumes the tones are
generated inside the dongle, and the two divisors were "measured on an
RTL2832U and are unverified anywhere else". A 25 MHz source could equally be a
device on the desk, a powered hub, or the monitor -- in which case it is a
fact about **this site**, not this receiver, and putting it in
`device_profile` would be exactly the mistake `.scratch/calibrating-the-flags/`
was opened to record. The test is cheap and is the first thing to do: sweep
the same range with the dongle on a different USB port and a different room,
and see whether 75.0005 and 150.0009 move, weaken, or stay.

**Whether one number can express it at all.** `reference_clock_hz` is scalar
and the derivation is hardcoded to /2 and /18. A second oscillator needs
either a list of clocks or a list of comb families, and inventing a
representation for one unconfirmed source is what ticket 02 refuses on the
rate-range hole. Do not add a field before the paragraph above has an answer.

**Whether the frequencies are even exact.** Both readings come from an
**uncalibrated** receiver: `--lte-chain` measured -30.8 ppm on this dongle,
which is 4.6 kHz at 150 MHz and 2.3 kHz at 75. The +877 Hz and +500 Hz offsets
from exact multiples are well inside that, so they neither confirm nor deny a
common fundamental. Re-measuring both **after a calibration** is what would
turn "within a kilohertz of a multiple" into evidence, and it costs one
`--calibrate lte` run.

## What it is worth

Not much on its own -- one unflagged candidate in one band. It matters because
of what it says about ticket 08: the marks were "measured on one dongle at one
site and compiled in", and this is the first evidence that even *this* dongle
at *this* site has an artifact the model does not cover. A second receiver
will not fix that; it will double it.

## What must be checkable

`check-suspect` covers the comb arithmetic against `RECEIVER_REFERENCE_HZ`.
Whatever comes of this, the property to pin is that a frequency belonging to
**no** modelled comb is not silently called clean -- today a candidate is
either receiver-like or unremarked, and "unremarked" is carrying two
different meanings.

## Comments

**2026-09-10, from a review of this ticket.** The third gate above is asking
the right question from the wrong model, and the answer was already in
`testfiles/`. A ppm error does not make a reading uncertain by 4.6 kHz -- it
*displaces* it by `f_tuned * d`, a constant per step, and a tone generated from
the same crystal has that displacement **cancel**, so it reads at its exact
nominal whatever the calibration.

`carrier_75000_bare.bin` (tuned 74 700 500, `ppm 0`) puts its carrier at
+299 478.2 Hz, stable to ~1 Hz over look lengths from 33 ms to 200 ms:
**74 999 978.2 Hz, 21.8 Hz below 25 MHz x 3**, against a predicted 2.30-2.43 kHz
of external displacement at the 30.8-32.5 ppm this dongle measures. One
percent of it.

**So the first gate is answered without a room change**: the family is coherent
with this receiver's reference, `device_profile` is the right home, and the
different-port/different-room test becomes corroboration. The two known comb
families calibrate the confirmation pass's precision on the way past -- they
read +159, +526 and +793 Hz from exact, about +/-800 Hz -- which puts 150.0009
at +900 Hz *inside* the precision and therefore internal too, and
**134.999939 at -61 Hz**, which `.scratch/am-airband/spec.md` had called a real
AM carrier.

The arithmetic, the refusals it needs (a calibrated receiver cannot run this
test -- the displacement vanishes and the internal tones move off exact
instead) and what it does *not* establish (the fundamental: 25 MHz is not
28.8/n) are in `11-what-a-frequency-offset-says.md`. Build that first; the
representation question in the second gate above is unchanged, but it is now a
question about at least three confirmed families rather than one unconfirmed
source.

## More of the family, 2026-09-11, and a pattern

An hour over 108-137 MHz found six clock-coherent lines, with the ppm measured
the same morning rather than quoted: **-35.96, locked, sem 0.04 over 498 FCCH
measurements**, so an external signal must be displaced **4.0-4.7 kHz** at
these frequencies. Every line below reads within 800 Hz of exact.

| measured | is | dB over floor |
| --- | --- | --- |
| 120.000427 | 1.6 x 75 | 48-70 |
| 127.999817 | 1.6 x 80 | 23.5 |
| 129.600159 | 14.4 x 9 | 27-50 |
| 135.999207 | 1.6 x 85 | 25.5 |
| 110.400513 | 1.6 x 69 | 46.0 |
| 115.197876 | 1.6 x 72 | **70.3** |
| **130.000000** | neither comb | 25.0 |
| **135.000000** | neither comb | 45.1 |

The two in bold are this ticket's family, and with the 75.000000 and 150.0009
already recorded that makes four members: **75, 130, 135, 150 MHz -- every one
a multiple of 5 MHz**, and none a multiple of 1.6 or 14.4.

**State that as a hypothesis, not a conclusion.** Against it: 110, 115, 120
and 125 are also multiples of 5 and none of them showed an unexplained line
(110.4 and 115.2 are the 1.6 comb; 125.121 has no carrier). So "multiples of
5 MHz" predicts lines that are not there, which a real comb family would not
do. What is solid is that four unexplained clock-coherent lines all happen to
be 5 MHz multiples, and that 5 MHz is not 28 800 000 / n -- so whatever
produces them is not a plain divider off the reference.

**And 115.197876 at 70.3 dB over its floor is the loudest thing this receiver
has ever been pointed at in that range.** Whatever else the comb model needs,
it is not a small effect: in the aeronautical bands the receiver's own spurs
are 20 dB above anything external, which is why
`.scratch/am-airband/issues/01-*` failed its gate today.

**What 130 and 135 MHz actually are, 2026-09-11:** bare carriers, not the
"modulated carriers" the survey called them. Measured at five channel widths,
130.000000's standing fraction tracks `P/(P + n*W)` -- a pure line in noise --
to within 0.03 at every one, reading "a bare carrier" once the channel is
1 kHz. The modulated verdict was the channel, not the signal: a narrow line in
a wide channel reads modulated however pure it is, and the confirmation pass's
channel is the candidate's measured bandwidth, which on a coarse sweep is
about a bin. `docs/aeronautical-vhf-at-this-site.md` section 4 has the table.

## Comments

**2026-09-11, from ticket 11 landing.**

**Gate one -- "whose 25 MHz is it" -- is answered: this receiver's.** Not by
the different-room experiment this ticket proposes, which is corroboration
rather than a prerequisite. `testfiles/carrier_75000_bare.bin` reads
74 999 978.2 Hz, **21.8 Hz** from 25 MHz x 3, where an external source at that
tuning must read kilohertz out. Whatever generates the family is clocked
coherently with this receiver's reference, which is what "belongs to the
receiver" can mean operationally. It says nothing about *which* oscillator or
which divider, and 25 MHz is still not 28.8/n.

**Gate three -- "whether the frequencies are even exact" -- is retired**, and
it was the wrong question. An uncalibrated receiver does not blur a reading, it
displaces it by a known amount, so the +877 Hz and +500 Hz this ticket calls
"well inside" the uncertainty are nothing of the kind: they are inside the
*precision*, about one 977 Hz bin, against 2.3 to 4.6 kHz of displacement. The
ticket had the sign backwards as well -- see 11's comments -- but not in a way
that changes this.

**Gate two -- whether one number can express it -- is untouched and is now the
whole ticket.** 150.0009 MHz comes back `unexplained` from the survey today
rather than silently clean, which is `SURVEY_SUSPECT_UNEXPLAINED` doing exactly
what this ticket asked for: the "unremarked" gap is closed as a *finding*.
Naming it the receiver's needs a grid containing 150.000000, and
`reading_origin_for()` will answer over any grid a caller proposes -- so the
arithmetic is waiting and the decision is not made. `reading_origin.h` refuses
to invent one, for the reason this ticket gives about the rate-range hole.

**One more piece of evidence for whoever takes it.** With the correction
applied, a 128-137 MHz sweep reads 131.204163, 129.604553 and 136.005188
against a coherent model predicting 131.204198, 129.604147 and 136.004352 --
35, 406 and 836 Hz. Three modelled families, each moved four kilohertz by
turning the correction on.

**2026-09-11, later: the family is measured, and the test proposed in the
paragraph above does not work.**

*The bad test first, because it is the instructive half.* "Sweep 75, 150 and
175 with the correction on and off and see whether the family moves with it"
discriminates **nothing**. Turning the correction on rescales the whole
frequency axis, so every reading moves by `f*k` -- coherent and external
alike. Measured, over a 976.6 Hz bin:

| reading | uncorrected | corrected | shift | `f*k` |
| --- | --- | --- | --- | --- |
| 75 MHz family | 75 000 488 | 75 002 441 | +1953 | 2388 |
| 150 MHz family | 150 000 488 | 150 005 371 | +4883 | 4776 |
| 174.718 (a real band III signal) | 174 713 379 | 174 718 262 | +4883 | 5562 |
| 74.877 (external) | 74 877 441 | 74 879 395 | +1954 | 2384 |

The external signal at 74.877 moved by the same relative amount as the family
member 123 kHz away from it. A shift that happens to everything is a property
of the axis, not of the source.

*What does discriminate is what ticket 11 already said*: the **absolute**
reading in an **uncorrected** sweep. A coherent tone reads its exact nominal;
an external one reads `f*k` low. Sweeping 2 MHz windows at `--ppm 0`, a
976.6 Hz bin, an 8 dB bar:

| tested | found | offset from exact | prominence | verdict |
| --- | --- | --- | --- | --- |
| 37.500000 | nearest 37.438965 | 61 kHz | 13.4 | **absent** |
| **75.000000** | 75.000488 | **+488 Hz** | 17.0 | **coherent** |
| **150.000000** | 150.000488 | **+488 Hz** | 11.2 | **coherent** |
| 175.000000 | nearest 174.718262 | 282 kHz | 12.3 | **absent** |
| 225.000000 | nearest 224.631348 | 369 kHz | 9.0 | **absent** |
| **300.000000** | 300.000488 | **+488 Hz** | 17.0 | **coherent** |
| 600.000000 | nothing over the bar | -- | -- | **absent** |

External would have read 2.4, 4.8 and 9.6 kHz low at the three that are there.
All three read **+488 Hz, which is exactly half a bin** (976.6/2 = 488.3) --
the quantisation of a tone reported at its bin's centre, and the same figure
at all three, so it is the grid and not the sources.

## What that settles, and what it does not

**It is not 25 MHz x n**, which is what this ticket and
`docs/what-is-on-air.md` assumed from 75.0005 being 25 x 3. 175 is 25 x 7 and
is absent at a 12 dB-prominence bar; 225 is 25 x 9 and absent.

**It is 75 MHz x 2^n** -- present at x1, x2, x4 and absent at the odd multiple
x3. That asymmetry is the finding: harmonic distortion of a 75 MHz oscillator
produces 150 **and** 225, and a chain of frequency doublers or dividers
produces octaves and never the third. So the shape to model is a **binary
chain**, not a harmonic comb, and `survey_comb_spacing_hz()`'s "a tone every
reference/n" cannot express it -- which is gate two, answered in the negative
for the representation that exists.

**It still does not say which oscillator.** 75 MHz is not 28.8/n and not
28.8*n; 28.8 x 125/48 is 75 exactly, which a fractional-N synthesiser would do
and which is a hypothesis rather than a measurement. "Coherent with this
receiver's reference" is what has been established and is all that has been.

**Nor whether it is inside the dongle.** Everything above says the source
shares this receiver's reference. `docs/receiver-artifacts.md` already records
that the comb sorts into two kinds -- tones made and heard entirely inside the
receiver, and tones the dongle radiates and hears back -- and nothing here
separates them. The unplug test still does that and nothing else does.

## What is left to do

A representation for a binary chain beside `survey_comb_spacing_hz()`'s
harmonic comb, and the evidence to justify it is above rather than assumed.
Two things to be careful of, both of which this ticket already argued:

- **Do not add a `device_profile` field for one unconfirmed source.** What is
  confirmed is a family on *this* receiver at *this* site. A second receiver
  is what turns "this chip does this" into a fact about a part, and
  `.scratch/calibrating-the-flags/` was opened to record exactly that
  distinction.
- **Whatever is added must keep a source with no clock silent.** A capture has
  no crystal to blame and must get no chain tests, for the reason it gets no
  comb tests.

600 MHz is worth one more look at a lower bar before the chain is called
three-deep; it was swept at the default 8 dB and found nothing, which is a
weaker statement than the four absences above.
