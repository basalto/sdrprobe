# LTE: whose network is this?

The LTE side finds a cell and reads its Master Information Block: bandwidth,
acknowledgement channel, antenna ports, frame number. It never learns **whose
cell it is**.

GSM took this step already. `gsm_bcch.c` goes past the synchronisation burst
to a System Information message and reports MCC 268, MNC 03, LAC 4010, cell
5131 -- the operator, the area, the identity. LTE stops one layer short of the
same thing, and System Information Block 1 is where it lives: the PLMN list
(MCC and MNC), the tracking area code, the cell identity, and whether the cell
is barred.

## Why this rather than a new technology

A survey of 24-1766 MHz at this site, re-measured where it mattered:

- **AIS** absent at a 3 s dwell on 161.5-162.5; **433 and 868 ISM** absent at
  5 s; **ACARS and VDL2** have no candidates on their channels.
- **DAB+** was measured absent by folding at its 96 ms frame (1.4 times its
  floor). **DVB-T** fails the receiver at 8 MHz, permanently. **5G NR on n28**
  was measured at 30 kHz spacing, so its synchronisation block is 3.81 MHz and
  does not fit either.
- **VHF airband** is the strongest real non-broadcast signal here -- 120.006
  MHz, prominence 39.1, confirmed -- and it is AM voice. A demodulator, not a
  decoder.
- **TETRA** is present, fits comfortably and would reuse the GMSK machinery.
  It is emergency-services spectrum and that question belongs to whoever owns
  the receiver, answered before code rather than after.

What is left is the cell that is already decoding. The live band 20 carrier
gave 168 messages in 175 blocks, so the chain underneath this is solid enough
to build on, and none of it needs a new sample rate: LTE already runs on its
own grid at 1.92 MS/s (ADR-0014).

It is also the least private thing on the air. A cell broadcasts SIB1
unencrypted to every handset in range, precisely so they can decide whether to
camp on it.

## The chain, and where it is unlike the MIB

The MIB is convolutional, 40 bits, and lands on fixed resource elements the
receiver can find from the synchronisation signals alone. SIB1 is none of
those things.

```
  PCFICH            how many symbols the control region occupies
  PDCCH             a downlink control message, its parity masked by SI-RNTI
  PDSCH             the resource blocks that message points at
  descramble        seeded by the cell identity and the subframe
  rate dematch      turbo, not convolutional: three streams and a filler
  turbo decode      rate 1/3 PCCC, two constituent encoders, a QPP interleaver
  CRC-24A           24 bits, and a different polynomial from the MIB's 16
  ASN.1 UPER        an unaligned packed encoding, not a fixed field layout
```

Two of those are new kinds of thing rather than bigger versions of what is
here. **Turbo coding** is iterative and soft in, soft out, where every decoder
in this repository so far has been Viterbi. **ASN.1 UPER** is a variable
layout driven by a schema, where every message so far has had fixed fields at
fixed offsets.

## What this must not become

A partial read that reports a PLMN it is not sure of. The GSM side sets the
standard here: the Fire code either passes or the message is not reported, and
`check-pipelines` pins MCC 268 MNC 03 against a real capture. SIB1 has a
24-bit CRC and there is no reason to report anything that fails it.

`testfiles/lte_b20_pci28.bin` must keep reading cell 32 under the normal
cyclic prefix in every block, whatever happens here.
