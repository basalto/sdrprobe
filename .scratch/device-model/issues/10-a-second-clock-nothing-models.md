# 10 - There is a second clock here, and the comb test cannot see it

Status: needs-triage
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
