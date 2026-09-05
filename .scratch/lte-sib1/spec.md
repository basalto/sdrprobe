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

## It does not fit the receiver, and this spec said it did

**2026-09-05.** The sentence above about the sample rate is wrong, and it is
the sentence the whole feature rests on.

`LTE already runs on its own grid at 1.92 MS/s` is true of the Master
Information Block and of nothing else. 1.92 MS/s is 128 subcarriers, which is
**six resource blocks, 1.08 MHz of usable grid** (ADR-0014). The MIB fits
because it is 72 subcarriers about DC, always, whatever the cell's bandwidth --
that is why the LTE chain works here at all.

The cell this was to be built on is **50 resource blocks, 9 MHz occupied**,
measured both ways:

```
$ ./sdrprobe --headless --lte-chain --earfcn 6200 --lte-chain-seconds 20
chain 1 mib ports 2 prb 50 phich normal 1/6 sfn 808 quarter 0 combining 1
$ ./sdrprobe --file testfiles/lte_b20_pci28.bin --headless \
      --technology lte --sample-rate 1920000 --decode --once
MIB  50 blocks (9.00 MHz)  PHICH normal 1/6  SFN 441  2 antenna ports
```

The control region is not central and not narrow. It occupies the first one to
three symbols **across the whole system bandwidth**, and its resource-element
groups are interleaved across every resource block on purpose, for frequency
diversity. Six blocks of fifty is twelve per cent of it, and a control channel
element is nine groups that the interleaver has scattered the length of the
band -- so not one of them can be assembled, let alone a downlink control
message decoded from it. SIB1's own resource blocks are then allocated by that
message anywhere in the fifty, commonly distributed.

Covering 9 MHz needs a sample rate above 10 MS/s. The R820T tops out near
2.4 MS/s and unreliably at 3.2. This is the same permanent failure the spec
above records for DVB-T at 8 MHz and for 5G NR's 3.81 MHz synchronisation
block, and it should have been the first thing checked rather than an
assumption inherited from the MIB.

**What would unblock it: a cell of six resource blocks**, 1.4 MHz, whose whole
bandwidth fits. Nothing else, and nothing here is known to be one:

| band | cell | bandwidth |
| --- | --- | --- |
| 20, EARFCN 6200 | PCI 28 | **50 blocks**, measured on air and on the capture |
| 8, EARFCN 3475 | PCI 330 | unknown -- 216 cells found in 219 blocks at PSS 0.9, and **not one MIB decodes**, so its bandwidth cannot be read |
| 28 | none | no LTE cell; band 28 here carries 5G NR (`probe-periodicity`) |

So the honest statement is narrower than "nothing here fits": the only cell
whose bandwidth can be read is five times too wide, and the only other LTE cell
at this site will not give up its Master Information Block. A 1.4 MHz carrier
is in any case an unusual deployment -- narrowband IoT and rural refarming
rather than a macro cell.

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
