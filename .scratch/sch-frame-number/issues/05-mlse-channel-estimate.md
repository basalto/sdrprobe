# 05 — MLSE Channel Estimate for Soft Viterbi

Status: open
Blocked by: 02

The joint soft Viterbi (ticket 02) was implemented using the fallback correlation metric (rewarding the product of the soft imaginary sample and the expected differential sign). While it improved raw block yield (22 to 25 blocks on the ARFCN 69 capture), single-burst data-field errors still occur because GMSK Inter-Symbol Interference (ISI) is not modeled.

To eliminate the single-burst T1/T2/T3 errors:
- Implement the Phase 2b channel estimate: Least-squares estimate a 3-tap channel `h[0..2]` from the known 64-bit training sequence and the derotated symbol samples.
- Update `sch_bit_cost` to use the Euclidean distance between the received derotated sample and the expected sample (predicted via `h` and the last 3 candidate channel bits).
- Remove the fallback correlation metric.

Verify that `make probe-gsm-chain` reports a constant `T1` across the single-burst decodes.

## Comments
