# 01 - Every cell on the carrier, not the strongest one

Status: ready-for-agent

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
