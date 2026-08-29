# 02 — Soft-decision receiver (channel estimate + joint soft Viterbi)

Status: resolved

**Note:** The soft-decision Viterbi was implemented with the simple correlation metric fallback (no `h` estimate). The MLSE channel estimate has been moved to ticket 05.
Blocked by: (none)

Replace the hard reconstruction (`gsm_dsp.c:420-440`) and `sch_viterbi` with a
soft-decision receiver run at the found burst position/timing:

- Least-squares estimate an L≈3–5-tap channel `h` from the known training
  symbols (models the GMSK ISI).
- Run one joint differential+convolutional soft Viterbi: state = (conv state ×
  last channel bit) = 32 states, 39 steps; branch metric from `h` and the soft
  symbol observations (correlation-metric fallback without `h`). Use the coded-
  bit ↔ burst-position map and anchors in `docs/sch-frame-number-decode.md` §5.3.
- Keep carrier refinement + training sync + parity + `sch_parse` unchanged.
- Must not regress BSIC.

Also update `scripts/gsm_chain_probe.c` stages to reflect the soft receiver
(print `h`, per-branch soft margins). Detail: §5.2–5.3.

## Comments
