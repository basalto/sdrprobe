# 01 — Front-end refinement (fine CFO, matched filter, sub-phase timing)

Status: reviewer
Blocked by: (none)

Sharpen the SCH front end before the soft receiver, in `src/gsm_dsp.c`:

- Estimate residual CFO from the 64 training symbols (mean differential-phase
  error vs `train_diff`) and de-rotate every symbol by it.
- Integrate the complex baseband over each symbol (short boxcar / Gaussian
  matched filter) before forming the symbol sample.
- Parabolic-interpolate the training-correlation peak across the 8 timing phases
  to a fractional timing.

Independent of ticket 02; reduces the raw error rate feeding whatever decoder.
Detail: `docs/sch-frame-number-decode.md` §5.1.

## Comments
