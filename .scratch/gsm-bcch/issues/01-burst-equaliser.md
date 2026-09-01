# 01 — A burst equaliser good enough for the Fire code

Status: ready-for-agent
Blocked by: (none)

`src/gsm_bcch.c` decodes a BCCH block from 456 soft bits and parses the System
Information inside it, checked by 491 checks. Nothing feeds it yet, because
recovering those soft bits off the air needs a demodulator this repo does not
have.

## What was measured

Working from `testfiles/gsm_arfcn_69.bin` with a throwaway probe (2 s, 31
blocks, ARFCN 69, BSIC 59):

- **31 of 31 blocks decode an SCH**, and 7 of those are at frame 1 of the
  51-multiframe, which is the one followed by a BCCH block in frames 2-5.
- **The bursts are where the arithmetic says they are.** Stepping
  `GSM_FRAME_SYMBOLS` (1250) from the SCH burst and correlating the BCC's
  training sequence gives scores of 300-414 at a shift of at most a symbol or
  two, on all four bursts of all seven blocks.
- **Coherent detection recovers the training sequence perfectly**: derotate by
  pi/2 per symbol, estimate a single complex tap from the 26 known symbols,
  project -- 0 errors out of 26, in every burst of every block.
- **And the data bits are 9-17% wrong.** A rate-1/2 K=5 code repairs perhaps
  3-4%, so 0 of 7 blocks passed the Fire code. Differential detection gave
  9-10%; single-tap coherent gave 9-17%.
- Sixteen combinations of burst order, half order, polarity and bit order were
  tried against the Fire code as an oracle. None passed, which rules out a
  convention error in the interleaving or the parity and points at the bits
  themselves.

## What it needs

The gap between a perfect training sequence and 10% data errors is
inter-symbol interference. GMSK with BT = 0.3 spreads each symbol across about
three, so a single tap fits the training pattern it was measured on and
mispredicts everything else. The standard answer is an MLSE equaliser: a
multi-tap channel estimate from the training sequence, then a Viterbi over the
channel trellis whose branch metric is how well a candidate symbol sequence
explains the received samples.

One attempt is recorded here as a warning: a 5-tap least-squares estimate with
a 16-state MLSE and max-log-MAP soft outputs made it *worse*, 14-22%. The
structure was right and something in it is wrong; it was not debugged. Start
from a known-good reference rather than from that sketch.

Note also the layering. `gsm_normal_bursts()` was written in `gsm_dsp.c` and
reverted, because the training sequences live in `gsm_bcch.h` and the Probe
context's DSP must not depend on the Decoder context's message layer. The
sequences are a physical-layer constant (GSM 05.02 5.2.3); move them into
`gsm_dsp` when the demodulator lands.

## How it will be known to work

The Fire code. Forty parity bits over 184 means a block that passes is right
or is a one-in-a-million-million accident, so a single pass on a recorded
capture settles it -- no ground truth needed. After that, `MCC 268` from a
capture recorded in Portugal is the confirmation, and
`.scratch/gsm-bcch/spec.md` records what to assert in `check-pipelines`.

## Comments
