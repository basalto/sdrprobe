# 03 — Validation: synthetic ISI+AWGN + real T1-consistency

Status: reviewer
Blocked by: 02

In `tests/gsm_dsp_test.c` (deterministic, `-lm` only):

- Add a Gaussian-pulse (BT=0.3) / 3-tap ISI + AWGN channel in the *test* (leave
  `gsm_sch_modulate` idealised). Loop N random (BSIC,T1,T2,T3), fixed seed, at
  ~8–10 dB; assert ≥95% correct T1/T2/T3, and strictly better than the current
  hard path on the same bursts.
- Add a real-capture test over `testfiles/gsm_arfcn_69.bin`: decoded T1 agrees
  across ≥90% of decoded blocks; keep the BSIC assertion.

The probe's consistency sweep should flip from "T1 disagrees" to "T1 consistent".
Detail: `docs/sch-frame-number-decode.md` §7.

## Comments
