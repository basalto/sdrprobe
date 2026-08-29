# Decode the SCH frame number with a soft-decision receiver

## Status

accepted (implementation pending — see `docs/sch-frame-number-decode.md`)

## Context and decision

`gsm_sch_decode()` recovers the BSIC reliably but the SCH reduced frame number
(T1/T2/T3) is not trustworthy: the diagnostic probe (`make probe-gsm-chain`)
shows a *clean* burst (perfect training match, no low-confidence symbols) still
flipping a single T1 bit that the 10-bit parity fails to catch. The cause is the
fully **hard-decision** chain — hard differential bits, integrate-then-hard
Viterbi — which discards per-symbol confidence, propagates a lone differential
error along the field, and cannot cope with the pattern-dependent bias of GMSK
inter-symbol interference.

We will replace the hard reconstruction + Hamming Viterbi with a **soft-decision
receiver**: a per-burst channel estimate from the known training sequence feeding
a **joint differential + convolutional soft-decision Viterbi** (state = conv
state × last channel bit), decoded directly from the soft symbol observations.
Carrier refinement and training sync stay as they are. Optionally, an
application-layer **frame-number lock** votes the constant T1 and predicts the
next burst's number across bursts.

## Considered options

- **Hard path + wider parity / range gates** — cannot repair the bit, only
  reject more; rejects too much and still misses single-bit errors.
- **Soft-LLR reconstruction → soft Viterbi** — removes the hard-decision loss but
  not the GMSK ISI error floor; kept as a fallback if channel estimation proves
  unnecessary.
- **Multi-burst voting only** — cheap and effective for the symptom, but leaves
  the single-burst decoder weak; adopted as a complementary layer, not the core.

## Consequences

- The decode core is rewritten, but the plugin stays **stateless**: the channel
  estimate and Viterbi are per-call. Only the optional frame-number lock adds
  state, and it lives outside the plugin (ADR-0009 precedent).
- Tests must inject **ISI + AWGN** to be meaningful (the idealised MSK modulator
  is too easy); the real capture is validated by T1 self-consistency, not a
  known absolute frame number.
