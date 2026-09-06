# 01 — Find which stage loses band 8's broadcast channel

Status: needs-triage

The spec has the evidence and the two hypotheses nobody has tested. This is
about picking one and measuring it, not about writing a decoder.

## Where to start

`make probe-lte-chain` already walks the chain and prints every stage; what it
does not print is *why* the parity does not fit, because at that point there
is nothing left to print. Two things would say:

1. **The cyclic prefix decision and its margin.** The chain reports `normal`
   and reports it as a fact. It is a correlation with a runner-up, like every
   other decision here, and the runner-up is not shown. If extended scores
   nearly as well on this signal, that is the answer and it is one number away
   from being visible.
2. **Whether the four broadcast symbols are being sampled where they should
   be.** The frame boundary comes from the primary sequence, whose peak here
   is 0.583 against a runner-up of 0.479. A timing error of a few samples
   costs the broadcast channel and costs synchronisation almost nothing, so
   the two can disagree.

The honest first move is to make the chain print what it already knows and
looked at nothing new: the cyclic-prefix correlation both ways, and the
timing's peak against its neighbours.

## What must be checkable

- Whatever is added prints for the *working* capture too, so the two can be
  compared. `testfiles/lte_b20_pci28.bin` decodes 28 messages in 50 blocks and
  is the control: a number that looks alarming on band 8 means nothing until it
  has been read on a cell that works.
- If the cyclic prefix turns out to be the answer, the synthetic buffers in
  `tests/lte_dsp_test.c` can carry an extended-prefix case, which they do not
  today -- `build_buffer` passes 0 for `extended_cp` at every call site.

## What this must not become

A second decoder for a cell nobody can hear. If the answer turns out to be
that this cell is simply too weak for its broadcast channel at this antenna,
that is a finding and the ticket closes: `wontfix`, with the number that says
so. The point is to know which stage loses it, not to make it decode.

## Answer

Status: both hypotheses measured and both ruled out. The fault is not in
synchronisation, and it is not weakness. It tracks **N_ID_2 = 0**, which is the
one value of the three that no working cell here has ever exercised.

Measured 2026-09-06 on a fresh capture,
`captures/lte_earfcn3475_20260906-075233.bin` — 12 blocks, 12 with a cell, 0
with a message, the same shape as on air. `probe-lte-chain` now prints both
measurements for every block, on this capture and on the control.

### 1. The cyclic prefix — ruled out, and it had never been measured

The verdict was real but the comparison behind it was not. `lte_cell_search`
loops `for (k = 0; k < 2 && best_score < LTE_SSS_CONFIDENT; k++)`, so the
search stops at the first prefix clearing 0.72 — and this cell's secondary
sequence reads 0.83. **The extended hypothesis was never tried**, and the chain
printed `normal CP` as though it had been.

Now measured after the decision, where it cannot change it:

```
band 8  PCI 330   CP normal 0.82-0.84   extended 0.55-0.66
band 20 PCI 28    CP normal 0.74-0.83   extended 0.48-0.71   (decodes)
```

The same shape on both. The prefix is normal and the margin is comfortable.

### 2. The timing — ruled out, twice

The peak has no competitor: sidelobe 0.22-0.29 against peaks of 0.73-0.97,
where the control reads 0.15-0.36 against 0.60-0.87.

And directly, which needs no theory: sweeping the frame start across
`+-30` samples under all three port hypotheses, **no start fits**. The control
put through the same sweep decodes from **-9 to +8 samples**, an eighteen-sample
window. The timing is not merely right, it is nowhere near the edge on a cell
that works, and no timing at all rescues this one.

One number is still unexplained and is *not* a fault: the shift
`pss_refine_timing` applies is +26 here against +10 on the control, stable in
both. The offsets differ by only 14% (-31.8 against -28.0 kHz), so the offset
alone does not account for it, and a modular-inverse model of the Zadoff-Chu
root predicts 30 and 10 against the observed 10 and 26 — so that does not
account for it either. Given the sweep above it costs nothing; noted so nobody
chases it as a lead.

### 3. "Too weak" — not supported

Two measurements of the equalised broadcast-channel elements were built and
**neither separates the captures**:

- distance from the ideal QPSK points: 58-66% on the control, 49-64% here;
- fourth-power QPSK coherence: mean 0.198 (0.120-0.373) on the control, 0.172
  (0.020-0.377) here, twelve blocks each at one port.

Both are recorded, including the one discarded, because a number that cannot
tell the control from the fault is not evidence — and the first was nearly
reported as one. The elements here look like the elements that decode, so the
parity failing is something deterministic rather than noise.

### 4. What it does track

Three real cells, all measured this morning:

| cell            | N_ID_2 | PSS root | CRS shift | result                  |
|-----------------|--------|----------|-----------|-------------------------|
| PCI 28, band 20 | 1      | 29       | 4         | decodes                 |
| PCI 59, band 20 | 2      | 34       | 5         | 215 messages / 292      |
| PCI 330, band 8 | 0      | 25       | 0         | **0 messages / 219**    |

PCI 59 was found by `--lte-scan 20` and walked with `--lte-chain --earfcn 6300`.
It is a third identity, a third root and a third reference-signal shift, and it
decodes. The cell that fails is the only one with **N_ID_2 = 0**.

The spec ruled the reference-signal shift out because `tests/lte_dsp_test.c`
builds synthetic cells at shifts 0, 4 and 5 and decodes all of them. That
argument does not survive contact with this repository's own history: those
buffers are built by the same functions that read them, so they cannot check a
convention both sides share — the conjugated primary sequence, the scattered
GSM SCH layout, the pi/4-DQPSK phase table and the TETRA scrambler seed were
all green throughout. **Shift 0 has never been read off the air here.**

## Next

Status stays `needs-triage`; this narrows it, it does not close it.

Shift and N_ID_2 are confounded in the evidence so far: `PCI mod 6 == 0`
implies `PCI mod 3 == 0`, so every shift-0 cell also has N_ID_2 0. They come
apart at **`PCI mod 6 == 3`** — N_ID_2 0 with shift 3. A cell like that decides
between "the broadcast path is wrong at shift 0" and "it is wrong for N_ID_2 0",
and there is no such cell among the three found. Scanning band 8 for more cells
is the cheap next move.

## Comments

The chain gained two things it did not have and one it should not keep:
`cell.cp_score[]` / `cell.cp_measured[]` (both prefixes, always measured, one
extra secondary-sequence read at an offset already known), and
`cell.timing_shift` / `cell.timing_sidelobe`. `check-lte-dsp` asserts both
prefixes are measured on every synthetic cell and that the reported verdict
matches the better score; reverting the extra read fails those four checks,
which is how they were shown not to be vacuous.
