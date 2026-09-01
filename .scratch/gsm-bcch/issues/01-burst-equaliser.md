# 01 — A burst equaliser good enough for the Fire code

Status: resolved
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

## Answer

Done, in `gsm_normal_bursts()` in `src/gsm_dsp.c`. `testfiles/gsm_arfcn_69.bin`
now yields seven System Information messages -- types 1, 2, 3, 4 and 13 --
saying **MCC 268, MNC 03, LAC 4010, Cell Identity 5131**. Portugal, NOS.

Three things were wrong, and each was found by measuring rather than reasoning:

**The channel model was strictly causal.** A GMSK pulse is centred, so a symbol
shows up in samples taken before it as well as after. Fitting the taps at each
possible alignment and reading the residual made it obvious: 27.6 at delay 0,
5.9 at delay 2, with taps `0.01 0.19 0.92 0.34 0.05`. The delay is now chosen
per burst by minimum residual.

**Residual frequency offset.** The give-away was where the errors fell. Mapping
them across the burst showed them clustered at both ends and absent in the
middle -- the channel is fitted on the training sequence in the centre and then
used at the edges, so leftover offset shows up as drift away from where it was
measured. Fitting the two halves of the training separately gives the drift per
symbol. This took a burst from 8 wrong bits to 0.

**The Fire code was wrong twice.** The generator had `D^18` where it should
have `D^17` -- one bit in a hex constant, `0x04840009` for `0x04820009` -- and
the parity is inverted, so a good codeword leaves the register full of ones
rather than empty, the same trick GSM plays on the SCH parity. Neither was
visible to 491 round-trip checks, because the encoder and the checker shared
both mistakes. What found them was a capture decoding the same 23 octets three
times, four hundred frames apart, every one leaving remainder `FFFFFFFFFF`.
Bits that repeat exactly are not noise.

Two smaller ones fell out of the same evidence: the octets pack least
significant bit first (the LAPDm fill octet reads `0x2B` that way and `0xD4`
the other), and a BCCH block is LAPDm format Bbis -- a length indicator and
then the message, with no address or control octet, so the protocol
discriminator is at octet 1 and not octet 4.

Ground truth throughout was the SCH burst: its 78 data bits are known once its
parity-checked fields are re-encoded, so the equaliser could be scored against
them. It went from 2-19 wrong out of 78 to **0 out of 78 on every burst**.

`check-pipelines` asserts the result, and both regressions bite: restoring the
`D^18` typo loses every block, and removing the frequency correction loses the
neighbour list.
