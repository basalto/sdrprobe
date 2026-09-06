# 06 - A spectral shape that is actually shape

Status: wontfix

Blocked on nothing. Reads the extent `survey_carrier.h` already measures.

## The gap, and the naming problem on top of it

`enum survey_shape` is called shape and measures **width**: five names --
tone, narrow, medium, wide, very wide -- from one number bucketed at 5 kHz,
50 kHz, 500 kHz and 3 MHz. That is useful and it is not shape. Two carriers of
identical width can be an OFDM block and a single filtered carrier, and the
survey calls both "medium".

Whether the enum keeps the name is an open question for this ticket, not a
decision: `survey_carrier_shape()` is on screen and in the saved JSON, so
renaming it is a file-format change under ADR-0016.

## Three measurements over the extent

- **Skirt steepness** -- the ratio of the width at 3 dB down to the width at
  20 dB down. A brick-walled OFDM block and a raised-cosine single carrier
  differ here by a lot, and neither differs from the other in occupied
  bandwidth.
- **Flatness** -- geometric mean over arithmetic mean of the in-band bins.
  Near one is flat: OFDM, or a spread signal, or noise. Far below one is
  peaked: a carrier with sidebands.
- **Symmetry** -- power either side of `power_centre_hz`. `survey_carrier.h`
  already notes that `centre_hz` and `power_centre_hz` "part company on a
  lopsided carrier"; this is the number that says by how much, and asymmetry
  is a real finding -- single sideband, a vestigial-sideband television
  carrier, or a mis-tuned measurement.

## Two traps, both about the transform rather than the signal

- **The skirts may be the window's.** A Hann window's own sidelobes fall away
  at a fixed rate, and past roughly 60 dB down that is what a steepness
  measurement reads -- the analysis, not the transmitter. Bound the
  measurement above the window's floor and say where the bound is.
- **A narrow carrier has too few bins.** At 977 Hz bins a 200 kHz FM station
  spans 200 bins and flatness means something; a 25 kHz TETRA carrier spans
  25, and the flatness of 25 bins is mostly the window's shape. Set a minimum
  bin count and refuse below it rather than returning a number nobody can
  use -- the same discipline as the delay spread's bounds in
  `src/lte_findings.h`, which names all three of its cases including the two
  where it cannot answer.

## What must be checkable

No receiver, over the existing captures (ADR-0012). The invariants worth
pinning are the *orderings*, not the values, because the values depend on the
transform size and the orderings do not:

- `lte_b20_pci28.bin` is flatter than `fm_rds_tsf.bin`.
- A synthetic tone in noise is the least flat thing measurable.
- A synthetic signal built symmetric reads symmetric; the same signal with one
  sideband removed does not.

## Comments

**2026-09-06 — wontfix. Built four ways, measured, and taken back out.**

None of the three numbers survives at this receiver's resolution, and the one
that nearly does fails in the direction that misleads: it calls an empty
channel OFDM.

### Skirt steepness and symmetry: a width on a real profile measures its ripple

Both wanted a width at a stated level below a reference. A single transform of
a modulated carrier swings several decibels frequency to frequency --
`survey_carrier.h` records the same fact about troughs, where taking the
lowest bin split every station into its own shoulders -- so a width taken from
one profile is measured from whichever point fluctuated highest.

| attempt | 25 kHz TETRA, 3 dB width | 200 kHz GSM | 1 MHz OFDM |
| --- | --- | --- | --- |
| own transform, unaveraged | 641 Hz | 1564 Hz | 15162 Hz |
| own transform, 8 segments | 1204 Hz | 3545 Hz | 26478 Hz |
| the survey's 128-window average | 2481 Hz | 1797 Hz | 7732 Hz |
| ...with a 90th-percentile reference | 16999 Hz | 2849 Hz | 14748 Hz |

The fourth attempt made it worse in a new way: measured 3 dB below the 90th
percentile rather than below the maximum, a **bare carrier read wider than
everything else**, which is exactly backwards. Getting a usable 3 dB width
needs the ripple under a few tenths of a decibel, which needs hundreds of
averages, which is more signal than a block holds.

Symmetry failed on its own variance rather than on resolution: two samples of
the same empty channel read **+0.025 and -0.512**, which is larger than most
of the asymmetries it was for.

### Flatness works, and fails in the direction that misleads

It is the one number the ticket's own check plan asked for and it delivers
that: an FM broadcast station 180 kHz wide reads 0.43 and an LTE downlink
reads 0.92, which is a single filtered carrier against a block of subcarriers
at comparable widths.

Then the controls:

| signal | flatness | reference | verdict |
| --- | --- | --- | --- |
| a bare carrier at 75.000 MHz | 0.307 | -107.1 dBFS | peaked |
| TETRA, pi/4-DQPSK | 0.330 | -83.8 dBFS | peaked |
| an FM broadcast station | 0.431 | -64.1 dBFS | peaked |
| an LTE downlink, OFDM | 0.924 | -96.6 dBFS | flat |
| **an empty channel** | **0.790** | -100.8 dBFS | flat |
| **another empty channel** | **0.995** | -106.7 dBFS | flat |

Noise is flatter than OFDM, and there is no way to tell "flat because it is a
block of subcarriers" from "flat because nothing is there". The mitigation
considered -- only read it after a carrier has been found -- does not hold
either, because an OFDM block has no standing carrier and
`carrier_over_noise_db` is low for it too. That is the same pair of readings
that made `carrier_power_fraction` need a second number in ticket 01.

A row that looked like evidence and was not: a 200 kHz GSM extent read 0.939
"flat", and its reference level is **-105.3 dBFS with every bin inside the
band**, meaning the extent chosen held no carrier at all and the number is
the noise floor's. Three of the four flat readings above are noise.

### What is left, and is not this ticket

The naming complaint at the top stands: `enum survey_shape` is called shape
and measures width. Renaming it is a file-format change under ADR-0016 and
worth doing on its own terms, with no measurement behind it and none needed.

The code is removed rather than left unused. Nothing consumes it, and this
repository keeps exactly one measurement with no consumer -- `lte_turbo`,
for a stated reason that does not apply here.

This is the third measurement in this effort built, measured and put back:
the blind symbol-rate search in ticket 02, the instantaneous-frequency
histogram in ticket 05, and this. In all three the arithmetic was fine and the
claim was false, and in all three the table found it.
