# Improving the GSM SCH reduced-frame-number (T1/T2/T3) decode

Status: plan (not yet implemented)
Scope: `src/gsm_dsp.c` SCH decode chain, its tests, and a diagnostic probe.
Related: ADR `docs/adr/0011-sch-frame-number-joint-trellis.md`,
tracker `.scratch/sch-frame-number/`, probe `scripts/gsm_chain_probe.c`.

---

## 1. Problem statement

`gsm_sch_decode()` reliably recovers the **BSIC** (NCC/BCC) of a GSM cell from a
real capture, but the **reduced frame number** — T1/T2/T3, from which the full
TDMA frame number is reconstructed — is **not reliable**. Corrupted frame
numbers slip past the 10-bit parity, so the value shown in the GSM decode view
(and returned by the decoder) cannot be trusted.

This document analyses *why*, then specifies the fix.

## 2. Evidence

Run `make probe-gsm-chain` (source `scripts/gsm_chain_probe.c`) on the committed
capture `testfiles/gsm_arfcn_69.bin`. It walks one block through every stage and
then sweeps all blocks. The salient output:

```
STAGE 4  best timing phase=0/8  position=12019  training match=1.000  invert=no
STAGE 5  mean |Im|: training=430.5  data=382.6
         low-confidence symbols (<0.35 mean): 0 / 148
STAGE 9  parity OK
STAGE 10 BSIC=45 (NCC 5, BCC 5)   T1=1634 T2=4 T3'=2 -> T3=21  frame=2167572

CONSISTENCY SWEEP
  block  0: BSIC=45 frame=2167572 (T1/T2/T3 1634/4/21)
  block  1: BSIC=45 frame=2166817 (T1/T2/T3 1634/3/31)
  block  4: BSIC=45 frame=2507110 (T1/T2/T3 1890/8/1)
  ... (blocks 4..30 mostly T1=1890/1891) ...
  BSIC agreement: 22/22 on BSIC=45  (reliable)
  T1 spread across capture: 1634..1891  (should be a single value)
```

Two facts pin the diagnosis:

1. **T1 is impossible.** T1 changes once every 1326 frames ≈ **6.1 s**; the
   capture is ~2 s. So T1 must be a single value (or adjacent) across the whole
   file. Instead it swings 1634↔1890 (a difference of exactly **256 = one bit**,
   T1 bit 8). The minority value (1634, blocks 0–1) is a **single-bit decode
   error**; the majority (1890) is almost certainly the true T1.

2. **The erroring burst is clean.** Block 0 has a perfect 63/63 differential
   training match, `invert=no`, and **zero** low-confidence symbols by the
   `|Im|` magnitude test — yet it still flips one T1 bit. So the error is **not**
   bulk additive noise on a weak burst.

## 3. Root-cause analysis

The current chain (`src/gsm_dsp.c`) is **hard-decision end to end**:

1. Downconvert at the FCCH-refined carrier.
2. Search 8 fractional timing phases × all positions; per symbol produce a
   **hard** differential bit `m[k] = (Im(conj(prev)·cur) < 0)`; correlate the
   hard bits against the differentially-encoded training (Hamming count).
3. **Reconstruct** the channel bits by *integrating* the hard `m` bits outward
   from the known training anchor (`gsm_dsp.c:420-440`): data2 forward from
   `d[p+63]=TRAIN[63]`, data1 backward from `d[p]=TRAIN[0]`.
4. **Hard-decision (Hamming-metric) Viterbi** over the 78 reconstructed coded
   bits (`sch_viterbi`).
5. 10-bit inverted parity + a T2/T3 range gate (`sch_parse`).

Four compounding weaknesses, all in the hard path, let a single symbol slip:

- **W1 — Hard decisions discard confidence.** Only the *sign* of `Im` survives
  the demod; the *magnitude* (how confident that decision is) is thrown away at
  the demod, the training correlation, and the Viterbi. Soft-decision decoding
  is worth ≈2 dB and, more importantly, lets the Viterbi trade a marginal bit
  against the code constraints instead of committing early.

- **W2 — Differential error propagation.** Reconstructing `d` by integrating the
  hard `m` means one wrong `m[k]` flips that bit **and every bit after it toward
  the anchor**. A lone symbol error becomes a run of wrong channel bits.

- **W3 — GMSK inter-symbol interference (ISI).** GSM is GMSK (BT=0.3): each
  symbol's Gaussian phase pulse spreads into its neighbours, so the one-symbol
  differential phase is a *pattern-dependent* biased estimate of `±90°`, not a
  clean one. For some bit patterns the biased phase lands on the wrong side of
  the decision boundary **even at high SNR** — exactly the "clean burst, wrong
  bit" the probe shows. A one-symbol differential detector has an ISI error
  floor that no amount of SNR removes.

- **W4 — Weak error detection.** The 10-bit parity (plus the T2/T3 range gate)
  misses a large fraction of single/double-bit patterns, so a corrupted frame
  number is *accepted*. BSIC survives only because it sits in the earliest coded
  bits, closest to an anchor and shortest in span.

**Conclusion.** The frame number fails because a *clean* burst can still carry a
sporadic single-symbol error (dominated by W3, amplified by W2), which the
hard-decision Viterbi (W1) cannot repair and the weak parity (W4) cannot reject.
The signal quality is adequate (SNR ≈ 12.6 dB, training 1.00); this is a
**decoder-quality** problem, not a capture problem.

## 4. Design options

| Option | Idea | Fixes | Cost |
|---|---|---|---|
| A. Joint soft differential+conv trellis | One Viterbi over (conv state, last channel bit); consume soft one-symbol differential observations directly | W1, W2 | moderate |
| A+. …with training-based channel estimate (MLSE) | Estimate the ~3–5-tap GMSK channel from the 64 known training symbols, use it in the branch metric | W1, W2, **W3** | moderate+ |
| B. Soft-LLR reconstruction → soft Viterbi | Keep structure, carry soft per-bit LLRs (decaying with distance from anchor) into a soft Viterbi | W1 (partly W2) | low |
| C. Multi-burst frame-number tracking | Vote T1 across bursts; predict/validate FN with a running counter | *symptom* (W3/W4 residue) | low |
| D. Front-end refinement | Matched filter + sub-phase timing + fine CFO from training | reduces raw errors | low |

The **chosen direction (Q1 = A)** is the soft-decision trellis. The probe
evidence (clean-burst single-bit errors ⇒ ISI dominates) means **A on its own
may not fully remove the error floor**; it must be the **A+ variant** that folds
in a training-derived channel estimate so the branch metric knows the true GMSK
pulse. Options C and D are cheap, complementary, and strongly recommended
alongside — C in particular *directly* cures the exact symptom the probe shows
(sporadic single-bit T1 errors) by majority-voting a quantity that is constant
over the capture.

> **Decision to confirm.** This plan upgrades the Q1=A "joint soft trellis" to
> **A+ (soft trellis with a training-estimated channel)**, because the probe
> shows ISI — not noise — is the dominant error. If you prefer to ship the
> simpler soft trellis first and add channel estimation only if the synthetic
> ISI test still fails, say so and Phase 2 below splits accordingly.

## 5. The chosen decoder (A+)

Replace steps 3–4 of the current chain with a single **soft-decision receiver**
run at the already-found burst position `p` and timing phase. Steps 1–2 (carrier
refinement, training sync) stay as-is — the 64-bit correlation is robust and
cheap. The new receiver has three parts.

### 5.1 Front-end refinement (Phase 1, cheap)

Before producing soft observations, sharpen the front end:

- **Fine CFO from the training.** Over the 64 known training symbols the
  expected differential is known (`train_diff`). Measure the mean residual phase
  `δ = mean(Δφ[n] − expected_±90°)` and de-rotate every symbol by `δ` per
  symbol. Removes the tens-of-Hz residual the probe shows varying block to block.
- **Matched filtering.** Integrate the complex baseband over each symbol (a short
  boxcar, or a Gaussian pulse ≈ the transmit filter) before forming the symbol
  sample. Lowers the noise on each observation.
- **Sub-phase timing.** Parabolic-interpolate the training-correlation peak
  across the 8 phases to a fractional timing, instead of snapping to 1/8.

These alone will not remove the ISI floor (W3) but they reduce the raw error
rate feeding the trellis and de-bias the observations.

### 5.2 Training-based channel estimate (Phase 2, the ISI fix)

Model the received symbol stream as the (differentially-decoded) channel bits
passed through a short linear channel `h[0..L-1]` (L ≈ 3–5), estimated once per
burst from the training:

- Take the known training channel bits `TRAIN[0..63]`, map to ±1 symbols, build
  the `(64−L+1) × L` convolution matrix, and least-squares solve
  `h = argmin ||X·h − y||²` where `y` are the received training symbol samples
  (after derotation). This is a tiny normal-equation solve (L×L).
- `h` captures the GMSK pulse + receiver response, i.e. the exact ISI. The
  branch metric in §5.3 uses `h` to predict the expected sample for a candidate
  bit sequence, so pattern-dependent bias (W3) is modelled instead of misread.

> This is "MLSE-lite": a per-burst channel estimate feeding a Viterbi metric.
> It is the standard GSM burst-equaliser structure, scaled down to what the SCH
> needs.

### 5.3 Joint differential + convolutional soft Viterbi (Phase 2)

One Viterbi recovers the 39 uncoded bits `u[0..38]` directly from the soft
observations, with **no hard `m` and no integration** (killing W1 and W2), and a
metric built from the channel estimate `h` (addressing W3).

**Bit ↔ burst-position map.** The burst channel positions (index `n` in the
148-symbol burst) are:

```
n = 0..2    tail (0)
n = 3..41   data1 = coded e[0..38]      (e[0] at n=3 … e[38] at n=41)
n = 42..105 training TRAIN[0..63]
n = 106..144 data2 = coded e[39..77]    (e[39] at n=106 … e[77] at n=144)
n = 145..147 tail (0)
```

Each coded bit `e[i]` maps to position `pos(i)` and has a differential neighbour
`d[pos(i)−1]`:

- data1: `pos(i) = 3 + i`. Neighbour of `e[0]` is the known tail bit 0; neighbour
  of `e[i>0]` is the previous coded bit `e[i−1]`.
- data2: `pos(i) = 106 + (i−39)`. Neighbour of `e[39]` is the known `TRAIN[63]`;
  neighbour of `e[i>39]` is `e[i−1]`.
- Two **closing** observations give free extra evidence: the transition into the
  training (`TRAIN[0] ⊕ e[38]`) constrains `e[38]`, and the transition into the
  trailing tail (`0 ⊕ e[77]`) constrains `e[77]`.

**Trellis.** State = `(conv_state ∈ 0..15, last_coded_bit ∈ {0,1})` → 32 states,
39 steps, 2 branches/step. For step `k` emitting the coded pair
`c0 = G0(u[k], conv_state)`, `c1 = G1(u[k], conv_state)`:

- For coded index `2k`: neighbour `b = anchor(2k)` if it has a fixed anchor else
  `last_coded_bit`; expected channel-bit pair drives the predicted sample via
  `h`; branch cost `+= metric(obs[2k], expected)`.
- For coded index `2k+1`: neighbour is `c0`; cost `+= metric(obs[2k+1], …)`.
- New state = `(((conv_state<<1)|u[k]) & 0xF, c1)`.

**Metric.** With the channel estimate, the per-symbol cost is the squared error
between the received symbol sample and the sample predicted from `h` and the
candidate channel bits over the last `L` positions (a standard MLSE branch
metric). Without `h` (Phase-2a fallback / simpler build), use the
correlation metric on the one-symbol differential: reward `s·Im` where
`s = +1` if the expected differential is 0 and `−1` if 1, i.e. minimise
`Σ −s·Im` (optimal for Gaussian noise on `Im`).

**Termination.** Tail bits force `conv_state = 0` at the end; pick the surviving
path of minimum metric among the two states with `conv_state = 0`, trace back to
`u[0..38]`.

Then run the **existing** parity check and `sch_parse` unchanged as a final
integrity gate.

### 5.4 Multi-burst frame-number tracking (Phase 3, optional but recommended)

Even a good single-burst decoder has a residual error rate. Exploit that the
frame number is **redundant across bursts**:

- **T1 vote.** T1 is constant over ~6 s; keep a short history and report the
  majority T1. (On the probe capture this alone turns 20/22 correct into 22/22.)
- **Counter predict/validate.** Once two bursts agree, the frame number advances
  deterministically with elapsed frames; predict the next burst's FN and accept
  a decode only if it matches (within tolerance), else fall back to the counter.

This introduces a small amount of *decoder state* — a **frame-number lock** —
analogous to the ADS-B position-pairing cache. Per ADR-0009's precedent, keep
`gsm_sch_decode()` **pure/stateless** and hold the lock in the application layer
(or a separate `struct gsm_sch_tracker`), not in the plugin's decode function.

If Phase 3 is adopted, add a glossary term to
`docs/contexts/decoder/CONTEXT.md`:

```
**Frame-number lock**:
The minimal running memory of the SCH frame number, used to vote a constant T1
and predict the next burst's number; it is not a scheduler or a clock.
_Avoid_: Clock, timer, scheduler
```

### 5.5 Findings from initial implementation

Phases 1 (front-end), 2a (soft Viterbi without channel estimation), and 3 (tracking) were implemented. The diagnostic probe against `testfiles/gsm_arfcn_69.bin` showed:
1. **Raw error improvement:** The soft Viterbi and front-end filter increased the number of correctly parsed bursts from 22/31 to 25/31.
2. **ISI floor remains:** Despite the soft metric, single-burst `T1` values still swing (1634 vs 1890). The simple correlation metric (`s·Im`) treats ISI as noise, meaning clean bursts with hostile bit patterns still incur single-bit errors that slip past the 10-bit parity.
3. **Tracker effectiveness:** The multi-burst frame-number lock (Phase 3) completely hides these sporadic errors from the user. It successfully votes `T1 = 1890` and predicts the forward timeline, creating a stable, locked display.

**Next Step:** To perfect the raw single-burst decoding, Phase 2b (MLSE Channel Estimation) must be implemented. This means solving for `h` and using it in the Viterbi branch metric instead of the simple `s·Im` correlation.

## 6. Phasing

| Phase | Deliverable | Expected effect |
|---|---|---|
| 0 | The probe (`scripts/gsm_chain_probe.c`) + this plan + ADR | Shared diagnosis (done) |
| 1 | Front-end: fine CFO + matched filter + sub-phase timing | Fewer raw errors; de-biased obs |
| 2 | Soft joint differential+conv Viterbi with training channel estimate; replace `sch_viterbi`+reconstruction | Removes W1/W2, models W3 |
| 3 | Multi-burst frame-number lock (app layer) | Cures residual single-burst errors |

Ship Phase 2 as the core; Phases 1 and 3 are independent and additive.

## 7. Validation (Q3 = both)

### 7.1 Synthetic, deterministic (`tests/gsm_dsp_test.c`)

The current round-trip modulates **idealised MSK** with light noise — too easy to
exercise the new decoder (both old and new pass). Make the synthetic channel
*bite*:

- Add a **Gaussian pulse (BT=0.3)** shaper to the test modulator (or a fixed
  3-tap ISI approximating it) so the synthetic signal has the same ISI as real
  GSM. *Decision:* keep the plugin's `gsm_sch_modulate` idealised (it is the
  reference encoder); put the ISI+AWGN channel in the **test**, not the plugin.
- Loop over N random (BSIC, T1, T2, T3) with a fixed seed at a target Es/N0
  (e.g. 8–10 dB) and assert the decoder recovers **T1/T2/T3** in ≥95% of bursts.
- Regression guard: assert the new decoder's frame-number success rate is
  strictly better than the old hard path on the same synthetic bursts.

### 7.2 Real capture (`tests/gsm_dsp_test.c` + the probe)

We have no ground-truth frame number, so assert **self-consistency** on
`testfiles/gsm_arfcn_69.bin`:

- Decode all blocks; assert the decoded **T1 agrees across ≥90%** of bursts (T1
  is constant over the capture window) — this is exactly what the probe's
  consistency sweep measures and currently *fails* (1634..1891).
- Keep the existing BSIC assertion (45 / NCC 5 / BCC 5).
- The probe's `CONSISTENCY SWEEP` conclusion should flip from "T1 disagrees" to
  "T1 consistent".

### 7.3 The probe as living documentation

`scripts/gsm_chain_probe.c` stays in the tree as the white-box walk-through and
regression narrative. Update it alongside Phase 2 so its stages reflect the soft
receiver (e.g. print the channel estimate `h` and per-branch soft margins).

## 8. Risks & notes

- **Metric scaling / numerics.** The correlation and MLSE metrics accumulate over
  ~80 symbols; use `double` (or scaled ints) and normalise per burst by the mean
  `|Im|` (the probe already reports it, ~400) to stay well-conditioned.
- **Channel-estimate order L.** Start L=3; the SCH training (64 symbols) easily
  supports L≤5. Too large overfits noise.
- **Compute budget.** 32-state × 39-step Viterbi with an L-tap metric is trivial
  (<a few µs); it runs once per successful sync per block, well within the
  ~65 ms block cadence.
- **Statelessness.** Keep `gsm_sch_decode()` pure (Phases 1–2). Only Phase 3
  adds state, and it lives outside the plugin (ADR-0009 precedent).
- **Scope guard.** BSIC decoding already works; the change must not regress it —
  the synthetic and real BSIC assertions guard that.

## 9. Deliverables checklist

- [ ] Phase 1 front-end in `src/gsm_dsp.c`.
- [ ] Phase 2 soft receiver (channel estimate + joint soft Viterbi) replacing the
      reconstruction + `sch_viterbi` path; parity/`sch_parse` unchanged.
- [ ] Synthetic ISI+AWGN frame-number test in `tests/gsm_dsp_test.c`.
- [ ] Real-capture T1-consistency test.
- [ ] Update `scripts/gsm_chain_probe.c` stages for the soft receiver.
- [ ] (Phase 3) `struct gsm_sch_tracker` frame-number lock in the app +
      `Frame-number lock` glossary term.
- [ ] Keep ADR `0011` in sync with what actually ships.
