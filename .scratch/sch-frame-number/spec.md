# SCH reduced frame number (T1/T2/T3) reliable decode

Status: reviewer

## Problem

`gsm_sch_decode()` recovers the BSIC reliably but the SCH reduced frame number
(T1/T2/T3) is not trustworthy. The `make probe-gsm-chain` sweep over
`testfiles/gsm_arfcn_69.bin` shows T1 swinging 1634..1891 across a ~2 s capture
where it must be a single value — a sporadic single-bit decode error that the
10-bit parity accepts.

## Root cause

The SCH decode chain is hard-decision end to end (hard differential bits,
integrate-then-Hamming Viterbi). It discards per-symbol confidence (W1),
propagates a lone differential error along the data field (W2), cannot cope with
GMSK ISI's pattern-dependent bias (W3), and the 10-bit parity misses the
resulting patterns (W4). Full analysis and evidence:
`docs/sch-frame-number-decode.md`.

## Decision

Replace the hard reconstruction + Hamming Viterbi with a soft-decision receiver:
training-based channel estimate → joint differential+convolutional soft-decision
Viterbi. Front-end refinement (fine CFO, matched filter, sub-phase timing) and an
optional application-layer frame-number lock (vote/predict) round it out. See
ADR `docs/adr/0011-sch-frame-number-joint-trellis.md`.

## Success criteria

- Synthetic ISI+AWGN test: ≥95% correct T1/T2/T3 at ~8–10 dB, strictly better
  than the current hard path.
- Real capture: decoded T1 agrees across ≥90% of bursts (probe consistency sweep
  flips from "T1 disagrees" to "T1 consistent").
- BSIC decode must not regress.

## Tickets

See `issues/`. Implement in phase order; 01 and 04 are independent of 02/03.

## Comments
