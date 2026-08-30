# 05 — MLSE Channel Estimate for Soft Viterbi

Status: wontfix
Blocked by: 02

The joint soft Viterbi (ticket 02) was implemented using the fallback correlation metric (rewarding the product of the soft imaginary sample and the expected differential sign). While it improved raw block yield (22 to 25 blocks on the ARFCN 69 capture), single-burst data-field errors still occur because GMSK Inter-Symbol Interference (ISI) is not modeled.

To eliminate the single-burst T1/T2/T3 errors:
- Implement the Phase 2b channel estimate: Least-squares estimate a 3-tap channel `h[0..2]` from the known 64-bit training sequence and the derotated symbol samples.
- Update `sch_bit_cost` to use the Euclidean distance between the received derotated sample and the expected sample (predicted via `h` and the last 3 candidate channel bits).
- Remove the fallback correlation metric.

Verify that `make probe-gsm-chain` reports a constant `T1` across the single-burst decodes.

## Comments

### 2026-08-30 — implemented, but it does not fix the frame number

The MLSE channel estimate is implemented as specified: a least-squares 3-tap fit
over the training sequence, a Euclidean-distance branch metric, and the
correlation fallback removed. One correction to the spec: the response is
**centred**, not causal. Fitting `y[n] = h0*a[n] + h1*a[n-1] + h2*a[n-2]` leaves
a normalised LS residual of 0.106; the centred form (a one-symbol decision
delay, taps 0.197/0.928/0.316) gives 0.064. The trellis therefore carries the
two preceding coded bits, 64 states rather than 32.

**It changes nothing measurable, because the premise was wrong.**

- On `testfiles/gsm_arfcn_69.bin` the MLSE build decodes all 31 blocks
  **bit-identically** to the previous correlation metric.
- On synthetic bursts through a symbol-spaced 3-tap ISI channel (the taps
  measured above) plus AWGN, both builds recover BSIC, T1, T2 and T3 on 100% of
  decodes at every SNR tested.

The single-burst decode is not ISI-limited. Evidence, all from the real capture:

- The burst finder is exact. Located bursts advance by whole frame counts
  (10, 11, 20, 21, 30, 31, 41 — the SCH pattern), and the sub-frame position
  drifts only 0.014 frames over 428, consistent with the receiver's ~33 ppm
  clock error. The capture is contiguous and the timeline is trustworthy.
- 19 of 31 bursts demodulate to an **exact valid codeword** — all 78 coded bits
  matching a re-encoded SCH message. The demodulator is not the problem.
- Yet no single frame-number origin explains the decodes: fitting `FN0` so that
  `FN = FN0 + elapsed_frames` puts only 2 of 31 bursts on one timeline, and the
  independently best-fitting FN per burst jumps by amounts (+652, -234, +1479)
  that are not even legal SCH spacings.
- It is not a field-layout error either. Every ordering of BSIC/T1/T2/T3' with
  each field MSB- or LSB-first was tested against the timeline; the standard
  reading is the best of all 384 and still scores 2/31.

So the burst is found correctly and its 25 information bits are recovered
correctly, but the frame number read from them contradicts where the burst sits
in the capture. The fault is downstream of demodulation and upstream of the
metric. **Next step is diagnosis, not more decoder work** — the open question is
whether `best_pos` identifies the right burst within a block when several are
present, and whether `sch_parse`'s reduced-frame-number reconstruction matches
what this network transmits. **Both now investigated in [[06-frame-number-diagnosis]]:
burst selection is exonerated; the interpretation of the information bits is
where the fault must be.**

Left `needs-triage` rather than resolved: the code is in, the stated goal
("`make probe-gsm-chain` reports a constant T1") is not met, and whether to keep
a 64-state trellis that buys nothing measurable is a call worth making
deliberately.


### 2026-08-30 — the premise is settled: it was the field layout

The frame number was never an ISI problem. `sch_parse` sliced the information
bits wrongly; see [[06-frame-number-diagnosis]]. With that fixed the frame
number decodes correctly on both captures, with the MLSE metric in place and
also, by every measurement taken, without it.

So the open question here is purely whether to keep a 64-state trellis that has
never shown a measurable gain. Dropping commit 51b6575 would revert it cleanly.
Worth re-running the synthetic ISI+AWGN comparison from ticket 03 first, now
that a correct decode exists to compare against — that measurement was made
while every field but BSIC was being misread.


### 2026-08-30 — measured and reverted

Re-ran the comparison once a correct decode existed to measure against. Both
builds are the current tree, differing only in the branch metric: the layout
fix is in both. Synthetic bursts, fixed seed, symbol-spaced 3-tap ISI using the
taps measured on ARFCN 69, AWGN calibrated against the burst's own RMS:

```
SNR dB      MLSE decoded    correlation decoded
   20           100%              100%
   12            99%              100%
    6            99%              100%
    0            86%              100%
   -3            59%               82%
```

Whenever either decodes, BSIC, T1, T2 and the full frame number are all 100%
correct. On the real captures: both give 31/31 on `gsm_arfcn_69.bin`; on the
weaker ARFCN 73 capture the correlation metric gets 16/30 against MLSE's 15/30.

So the channel estimate is not neutral, it is a **regression**. Estimating
three complex taps from 64 training symbols costs more in estimation noise at
low SNR than the modelled ISI returns, while the correlation metric has nothing
to estimate and degrades gracefully. It also cost 2x the trellis states (64
against 32) and about a hundred lines.

Reverted. The ticket's premise was wrong from the start — the frame number was
a field-layout bug, [[06-frame-number-diagnosis]] — so there was never ISI
damage for it to repair. Closing as wontfix rather than resolved: the work was
done, measured, and correctly rejected.
