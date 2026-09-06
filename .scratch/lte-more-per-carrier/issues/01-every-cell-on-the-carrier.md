# 01 - Every cell on the carrier, not the strongest one

Status: resolved

`lte_cell_search()` reports one cell per block, and a carrier can hold several.
Measured 2026-09-06 on EARFCN 3625: an 85-block run found **PCI 190 in 40
blocks and PCI 402 in 42**, which the chain presented as one cell changing its
mind. Their levels differ by 1.7 dB, so nothing about the samples was
ambiguous -- the search simply has one answer to give.

## Where to start

`lte_pss_detect()` already correlates all three Zadoff-Chu roots at every
offset and keeps `best_by_root[r]`, the best score each root reached. What it
does not keep is *where* each root peaked, so only the winner survives.

The two cells above have N_ID_2 of 1 and 0 -- different roots -- which is the
common case for co-channel neighbours and the one worth having first.

1. Keep the peak position and offset per root, not only per block, and let
   `lte_pss_detect()` pick its winner from those rather than from a running
   best. That is a rearrangement, not a new measurement.
2. Extract the stage after the primary sequence -- the integer sweep, both
   cyclic prefixes, the secondary sequence and the frame boundary -- into a
   helper taking a `struct lte_pss_result`. `lte_cell_search()` becomes that
   helper over the best root; the new entry point runs it over every root that
   detected.
3. Rank what comes back by `lte_reference_power()`, which already exists and
   is what tells 190 from 402.

Two cells sharing an N_ID_2 at different frame timings need peak *suppression*
rather than per-root peaks, and are deliberately out of scope: the case above
is real and measured, that one is not.

## What must be checkable

- A synthetic buffer holding two cells at different roots must return both,
  with the right identities, and in level order (ADR-0012, no receiver).
- One cell must still come back as one cell -- the existing captures decode
  unchanged, which `check-lte-dsp` already asserts.
- The headless report gains the second cell; `check-pipelines` can assert it.

## What this must not become

A search that reports a cell for every root that reaches a middling
correlation. Three roots always produce three numbers, and two of them are
usually noise -- the existing correlation and margin gates are what stop that
and they apply per root, not once per block.

## Progress

`lte_cell_search_all()` exists and is checked. The refactor the ticket asked
for is done: `lte_pss_detect` keeps each root's peak and offset rather than
only the winner's, the stage after it is `cell_from_pss()` taking a
`struct lte_pss_result`, and the new entry point runs that over every root
that detected, ranking by `lte_reference_power()`.

**One gate had to be added that the ticket did not anticipate.** Per-root PSS
and SSS gates are not enough: the secondary sequences of different N_ID_2 are
the same two m-sequences at different shifts, so a strong cell correlates well
enough with *another* root's candidates to clear both, and a single-cell
synthetic buffer duly reported two. A reported identity now also has to
predict its own reference signals -- `lte_port_coherence()` above
`LTE_PORT_COHERENCE_PRESENT`, a constant chosen by measuring both sides (live
ports 0.73-0.92, silent ports 0.33-0.41, chance 0.30). A PCI that came out of
a lucky secondary correlation does not read a whole-block sequence keyed on
the full identity.

**And a limit, measured rather than assumed.** A second cell is found only
when it is within a couple of decibels of the first. Sweeping a synthetic
carrier from -16.5 dB to -1.4 dB, the second cell comes back at -1.4 and
nowhere below it, because both cells transmit across the whole measured
bandwidth and the weaker one's secondary sequence is read *through* the
stronger one's transmission -- interference, which integration does not
remove. `check-lte-dsp` pins both ends: two cells at 1.4 dB apart, one at
6.9 dB. The real pair differ by 1.7 dB, so this covers the measured case and
only just.

## What the air says, and why this is not finished

`--lte-chain` now prints a `neighbour` line per block. Over 352 blocks on
EARFCN 3625:

```
primary      pci 402  171 blocks  mean SSS 0.74
             pci 190   98 blocks  mean SSS 0.59
             pci 163   21 blocks  mean SSS 0.58
             six more, 1 to 4 blocks each, SSS 0.47 to 0.57
neighbour    pci 410   50 blocks  mean PSS 0.37  RSRP -33.5 dBFS
             pci 406    9 blocks  mean PSS 0.48  RSRP -33.4 dBFS
```

The pair the ticket was written for -- 402 and 190 -- is confirmed and is the
top of the list by a wide margin. Everything else is unresolved, and **the
honest position is that this cannot say whether PCI 410 is a third cell or an
artefact.** It repeats in fifty blocks, which is not nothing; its primary
sequence reaches 0.37, which is barely over the gate; and the six singletons
in the primary column show this carrier produces spurious identities freely.

That is the next piece of work and it is not more detection. `lte_scan`
already has the discipline -- three looks, then a confirmation pass with five
more, dropping any identity that cannot say the same thing twice -- and the
chain has none. **A single block's identity was never evidence in this
repository**, and a neighbour list assembled from single blocks inherits every
false positive the gates let through. Until that exists, the `neighbour` line
is a per-block observation carrying its own correlations so a reader can
judge, and nothing should quote it as a cell.

## Answer

Status: resolved. The search finds every cell on the carrier, and a separate
verdict says which of them are real -- because the search's own output turned
out not to be trustworthy on its own.

**PCI 410 is not a cell.** The open question from the previous pass is settled
by the strongest test available rather than by a threshold: over 364 blocks on
EARFCN 3625 it was reported **59 times and decoded a broadcast channel none of
them**, while PCI 402 read 179 messages in 179 looks and PCI 190 read 16 in
104, at the same reference power.

That result also disposes of the confirmation rule this ticket assumed. A
count of blocks cannot settle a cell identity, and the reason is not
statistical: `lte_cell_search_all` mistakes a sidelobe for a cell and **makes
the same mistake every block**, so a false identity repeats exactly as
faithfully as a true one. Any threshold on hits would have confirmed 410, and
it was the more-seen of the two unconfirmed-versus-confirmed pairs.

A Master Information Block cannot be repeated into existence: it is scrambled
with the cell identity and checked by a sixteen-bit CRC, so it does not fit
unless the identity is right. `src/lte_confirm.h` is that rule --

```
lte-chain-cell pci 402 looks 179 messages 179 confirmed
lte-chain-cell pci 190 looks 104 messages  16 confirmed
lte-chain-cell pci 410 looks  59 messages   0 unread
lte-chain-cell pci 406 looks   9 messages   0 unread
lte-chain-cell pci 163 looks   7 messages   0 unread
lte-chain-cell pci 187 looks   1 messages   0 spurious
```

-- with three verdicts rather than two, because "seen repeatedly and never
read" is its own answer and is not the same as either "real" or "noise". A
weak but genuine cell lands there too, and saying so is more honest than
guessing which.

Two messages are required rather than one, and the arithmetic is already in
`view_lte.c`: thirty-six chances a block means a long run sees one parity pass
by luck. `check-lte-confirm` pins the whole rule, including the case this was
written for -- twenty-nine sightings and no message is `unread`, three
sightings and two messages is `confirmed`.

## What is left

The neighbour search itself still produces those artefacts; confirmation
labels them rather than preventing them. Suppressing the peak of a found cell
and searching again -- which is also what would reach two cells sharing an
N_ID_2 -- is the thing that would cut them off at the source, and is a
separate ticket if anyone wants it. Nothing downstream should read an `unread`
identity as a cell in the meantime.
