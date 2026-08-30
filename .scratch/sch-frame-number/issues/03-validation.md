# 03 — Validation: synthetic ISI+AWGN + real T1-consistency

Status: resolved
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

### 2026-08-30 — partial evidence from ticket 05

A scratch harness (not committed) already answers half of this ticket: synthetic
bursts through a symbol-spaced 3-tap ISI channel plus AWGN, fixed seed, decoded
by both the MLSE build and the previous correlation build. Both recover BSIC,
T1, T2 and T3 on 100% of decodes at every SNR tried. A committed version needs a
calibrated noise model — mine still decoded everything at a nominal -12 dB, so
its SNR axis is not trustworthy and it never found the cliff where the two
metrics might separate.

The real-capture half of this ticket cannot pass yet. T1 is still not consistent
across the capture, and ticket 05 shows why that is not a decoder-quality
problem. Recommend keeping this blocked until the frame-number diagnosis in 05
lands.


### 2026-08-30 — the real-capture half is done

The real-capture validation this ticket asked for now exists in
`test_sch_real_capture()`, and in a stronger form than "T1 agrees across 90% of
blocks": the frame numbers must increase, must span no more than the capture's
own ~433 frames, and T1 must vary by at most 1. On `gsm_arfcn_69.bin` all 31
blocks decode and all three hold.

This is what caught the layout bug in [[06-frame-number-diagnosis]], and it is
the check the old suite lacked — everything it had could be satisfied by an
encoder and decoder sharing the same wrong field layout.

Still open: the synthetic ISI+AWGN half. Note its original purpose (proving the
soft receiver beats the hard path) is moot — see [[05-mlse-channel-estimate]] —
so it is now just a noise-robustness bound, worth having but no longer a gate.


### 2026-08-30 — resolved

Both halves are now in `tests/gsm_dsp_test.c`.

**Synthetic channel** (`test_sch_synthetic_channel`): 60 random
(BSIC, T1, T2, T3') bursts, own PRNG with a fixed seed so the bursts and the
noise are byte-identical every run. `gsm_sch_modulate` stays idealised; the
channel lives in the test — a symbol-spaced 3-tap ISI using the taps actually
measured on `gsm_arfcn_69.bin` (0.197 / 0.928 / 0.316), then AWGN referenced to
the burst's own RMS so the SNR axis means something. Asserts that the
soft-decision trellis recovers **every** field — BSIC, T1, T2 and the full
frame number — on at least 95% of bursts, and that it strictly beats the
hard-decision fallback on the same bursts. Measured at 3 dB: soft 60/60, hard
45/60. Runtime cost is +0.6 s.

The ticket asked for BT=0.3 Gaussian-pulse ISI; the taps used instead are the
ones the real capture actually exhibits, which is a better target than a
nominal pulse shape and is where the 3-tap length came from in the first place.

**Real capture**: `check_real_capture()` runs over both fixtures
(`gsm_arfcn_69.bin` and the second cell `gsm_arfcn_73.bin`) and asserts more
than the T1 agreement this ticket asked for — the decoded frame numbers must
increase, must span no more than the ~433 frames the capture covers, and T1
must vary by at most 1. Rebuilt against the pre-fix field layout, both fixtures
fail, on overlapping but different assertions.

The probe's consistency sweep has flipped as this ticket predicted it should:
`make probe-gsm-chain` now reports 31/31 blocks decoded, BSIC 59 throughout,
and T1 793..794.

One requirement is dropped as moot: "strictly better than the current hard
path" was written to justify the phase-2 soft receiver over the phase-1 hard
one, and that comparison is kept. The *separate* MLSE comparison from
[[05-mlse-channel-estimate]] is not, because that metric was measured as a
regression and removed.
