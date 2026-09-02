# FM broadcast: reading RDS

## Why this one

The 2026-09-02 full-range sweep (`surveys/2026-09-02-24M-1766M.md`) puts FM
broadcast 14 dB above everything else on the air here -- 22 candidates, the
strongest at -7.7 dBFS. It is also the only strong thing in the sweep that this
program could read and does not already.

The alternatives were checked rather than guessed, which is the only reason
this is the answer:

- **DAB+** was the first choice. 1.536 MHz fits the receiver comfortably and
  the OFDM and Viterbi machinery from the LTE work would have carried over
  almost whole. Folding band III at DAB's 96 ms frame gives a ratio of 1.4:
  there is no DAB here to decode.
- **DVB-T** is 34 candidates and 8 MHz wide. The receiver reaches about
  2.4 MS/s. The same wall that stopped n28.
- **5G NR on n28** is measured, understood, and out of reach at 30 kHz
  spacing (`.scratch/nr-cell-search/`).
- **TETRA** is present at -40 dBFS and its synchronisation burst is close
  kin to GSM's SCH, so it would reuse a great deal. It is set aside rather
  than ruled out: it is emergency-services spectrum and the question of what
  may be decoded from it deserves an answer before code, not after.

And the payload was confirmed present before proposing to decode it: the
19 kHz pilot sits 32 dB over the noise and the 57 kHz band 10 dB over it, on
`89.6 MHz`. Both subcarriers are suppressed, so this had to be measured as a
band -- a tone measurement at 57 kHz finds nothing by design, and did.

## What it delivers

A station's identity and what it is saying about itself: the programme
identification code, the eight-character programme service name, the programme
type, and radio text. That is the same shape as GSM's MCC/MNC/LAC/CI and LTE's
Master Information Block, so it lands on the Decoder side of the context map
beside `gsm_bcch.c` and `lte_mib.c`.

## The chain

| stage | what |
| --- | --- |
| discriminator | the angle each sample turns from the last; FM to baseband |
| pilot | 19 kHz, recovered coherently |
| subcarrier | 57 kHz is exactly three times the pilot, so the carrier is *given* rather than searched for -- no blind loop |
| symbols | 1187.5 bps, which is 57000/48, biphase over a differential encoding |
| block | 26 bits: 16 data and a 10-bit check from a shortened cyclic code, with one of five offset words added so block boundaries are found from the check itself |
| group | four blocks, 104 bits; block 1 is always the programme identification |

The offset words are the interesting part and the reason this is a decoder
rather than a demodulator: there is no preamble anywhere in RDS. Synchronisation
is found by sliding the block code over the bitstream and seeing which offset
makes the syndrome vanish, which is a search -- so it needs a floor before its
answer means anything, exactly like every other search in this repository.

## Cost, honestly

It reuses less than an OFDM technology would: no FFT path, no Viterbi, no
resource grid. What it reuses is the plugin seam (ADR-0001), the Probe/Decoder
split, and the discipline. The new DSP is a discriminator and a handful of
filters; the new decoder is a cyclic code and a group parser. Small, and none
of it is at risk of not fitting the receiver.
