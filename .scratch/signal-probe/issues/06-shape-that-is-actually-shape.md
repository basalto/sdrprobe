# 06 - A spectral shape that is actually shape

Status: needs-triage

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
