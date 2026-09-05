# 03 — Descramble, decode, and check the synchronisation block

Status: needs-triage
Blocked by: 02

From a located burst to a block worth believing: descrambling, the
rate-compatible punctured convolutional code, de-interleaving, and the CRC that
decides whether any of it is reported.

The synchronisation burst is findable before the network is known because it is
scrambled with a **known** sequence rather than with the network's own colour
code -- which is the whole reason a terminal can camp on a network it has never
seen. That fact is what makes this ticket possible at all and is worth stating
where somebody will meet it.

**RCPC is new here.** This repository has rate-1/2 and rate-1/3 convolutional
codes and a turbo code, and no punctured one. Puncturing is the same class of
trap as LTE's rate matching: a puncturing pattern that is wrong still produces
the right number of bits, and only the parity notices.

## What must be checkable

- The puncturing pattern against a property rather than against its own
  encoder, the way `lte_transport`'s two polynomials and column permutation are
  (`.scratch/lte-sib1/issues/02`). A pattern both sides share round-trips
  perfectly and fails only on air.
- A synthetic block through scramble, encode, puncture, interleave and back.
- **The real capture: a block whose CRC passes.** Nothing below that is
  reported, which is the standard `gsm_bcch` set.

## What must not be reported

Anything that fails the parity. A colour code with no check behind it is a
number, not a finding.
