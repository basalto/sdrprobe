# 03 — Descramble, decode, and check the synchronisation block

Status: ready-for-agent
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

## Comments

**2026-09-05 — needs-info: blocked on the standard, not on work.**

Everything this ticket needs is a constant from ETSI EN 300 392-2 -- the
scrambling sequence, the RCPC puncturing pattern, the interleaving, and where
the fields sit inside the 510 bits -- and the document is not available here.

None of it can be measured out of the signal the way ticket 02 measured the
burst grid. A grid is a *structure* and shows itself; a puncturing pattern is a
*convention* and does not. Guessing one produces a decoder that round-trips
perfectly against its own encoder and reads nothing off the air, which is the
failure mode this repository has already paid for twice.

So: get the document, or leave this closed. `tetra_burst_find()` and the
demodulator underneath it are done and checked either way, and ticket 02's
comment records what they found.

**2026-09-06 — unblocked. The document is available.**

ETSI publishes free of charge and EN 300 392-2 V3.8.1 (2016-08) downloads from
etsi.org. Everything this ticket was waiting on is in it: clause 8 for the
scrambling, clause 8.2 for the channel coding and the RCPC puncturing, and
table 9.9 for where the fields sit in the 510 bits of the synchronization
burst -- 12 bits of normal training sequence 3, 2 phase adjustment, 80
frequency correction, **120 scrambled synchronization block 1**, 38
synchronization training sequence, 30 scrambled broadcast, 216 scrambled block
2, 2 phase adjustment, 10 normal training sequence 3.

The burst position is already known on air to the symbol: the training sequence
matches 19 of 19 at symbols 108 to 126 of the slot, so block 1 is the 120 bits
immediately before it and the broadcast bits the 30 immediately after.

One warning carried forward from ticket 02, since it applies with more force
here: a puncturing pattern is a *convention*, so a transcription error
round-trips perfectly and reads nothing. The phase table was wrong for a day
under exactly those conditions. Check the transcriptions against something
outside them -- the CRC passing on air is the only real one.
