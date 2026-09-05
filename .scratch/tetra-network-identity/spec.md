# TETRA: whose network is this?

The strongest thing on air here that this program cannot read is in the TETRA
allocation. GSM already answers "whose network is this" from a broadcast
channel -- MCC 268, MNC 03, LAC 4010, cell 5131 -- and LTE answers half of it
from a Master Information Block. This is the same question asked of a different
technology, and the machinery is closer to GSM's than anything else here.

## Scope, decided before any code

**The broadcast layer only**: the synchronisation burst, and the network
identity a base station broadcasts to every terminal in range so it can decide
whether to camp -- colour code, frame and multiframe number, mobile country and
network code, location area. That is the direct analogue of `gsm_bcch.c`, and
it is the layer the rest of this program stops at.

**Not traffic.** Voice on a real network is encrypted end to end or air
encrypted, so it is not reachable in any case, and short data is somebody's
message rather than the network's announcement of itself. The receiver's owner
was asked before this was written, which is what
`.scratch/lte-sib1/spec.md` says the question deserves.

## Both gates, measured

### Does it fit the receiver?

Comfortably, and it is the first technology considered here that does. A TETRA
carrier is **25 kHz**; the receiver manages 2.4 MS/s. DVB-T needed 8 MHz and
5G NR n28 needed 3.81, and both failed permanently; LTE's SIB1 needed 9 MHz of
a 10 MHz cell and failed today. A 25 kHz channel is two orders of magnitude
inside the budget, so the whole signal is captured whole with room to spare.

### Is it actually there?

An allocation is a lookup (ADR-0015), so it was measured rather than believed.

A sweep of 380-400 MHz at 0.3 s dwell finds **18 carriers and confirms 17 of
them**, the strongest at -34.9 dBFS standing 28.0 dB above its floor. That says
something is transmitting; it does not say what.

TETRA is pi/4-DQPSK at **18 000 symbols per second**, and a linearly modulated
signal with excess bandwidth puts a spectral line at its symbol rate in the
squared magnitude. Measured on a three-second capture at 392.64 MHz, against
the surrounding floor:

| tuned to | what the survey found there | 18 kHz line / floor |
| --- | --- | --- |
| 392.640 MHz | the strongest carrier, -34.9 dBFS | **9.95** |
| 392.840 MHz | a carrier at -40.1 dBFS | 5.53 |
| 392.480 MHz | near a carrier | 3.79 |
| 393.340 MHz | empty | 1.96 |
| `testfiles/fm_rds_tsf.bin` | FM broadcast, a control | 0.79 |

The line tracks the carriers and dies away off them, and an FM station shows
nothing. **That is consistent with TETRA and inconsistent with analogue FM, and
it is not conclusive**: the statistic's own floor in this capture is 2 to 4 and
the carrier reads 9.95, a factor of three to five rather than the order of
magnitude that would settle it. Ticket 01 settles it properly, by demodulating.

An envelope fold at TETRA's 56.67 ms frame and DMR's 30 ms was tried first and
is recorded here as the wrong instrument, so nobody repeats it: a TETRA base
station transmits **continuously** on its main carrier, so the frame structure
is in the symbols and not in the envelope. An arbitrary 90 ms control scored
higher than either, which is what a meaningless statistic looks like.

## The chain

```
  downconvert       one 25 kHz channel out of the 2 MS/s block
  matched filter    root-raised-cosine, roll-off 0.35
  timing            18 000 sym/s does not divide 2 MS/s, so a loop, not a
                    decimation -- unlike fm_dsp, which chose its rate to avoid
                    exactly this
  pi/4-DQPSK        differential: the phase step carries the dibit
  burst sync        correlate for the synchronisation burst's training sequence
  descramble        the synchronisation burst uses a known scrambling, which is
                    what makes it findable before the network is known
  RCPC + interleave rate-compatible punctured convolutional, then de-interleave
  CRC-16            and only then is a block worth reporting
  BSCH              colour code, frame, multiframe, timeslot
  BNCH              mobile country code, network code, location area
```

Two of those are new here and the rest is close kin to GSM. The **timing loop**
is new because every previous decoder either had an integer relationship to the
sample rate or a training sequence at a known offset. **RCPC** is new: this
repository has rate-1/2 and rate-1/3 convolutional codes and a turbo code, but
not a punctured one.

## What this must not become

A demodulator. `.claude/skills/rf-environment` ranks a technology on whether it
ends in something the transmitter is *saying*, and airband was set aside for
exactly this reason. If this reaches a stable constellation and stops, it has
produced a picture, not an answer.

And not a partial read. GSM sets the standard: the Fire code either passes or
the message is not reported, and `check-pipelines` pins MCC 268 MNC 03 against
a real capture. A colour code with no parity behind it is a number, not a
finding.
