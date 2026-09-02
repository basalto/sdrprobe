# 02 — PBCH to Master Information Block

Status: ready-for-human
Blocked by: 01

`src/lte_mib.{c,h}`, the Decoder-side layer, and `make check-lte-mib`. The LTE
analogue of `gsm_bcch.c`: soft bits in, a message out, no samples and no
receiver.

| Layer | What |
| --- | --- |
| scrambling | Gold sequence, c_init = PCI, 1920 bits over 40 ms |
| rate matching | sub-block interleave + repetition, 120 -> 1920 |
| convolutional | rate 1/3, K = 7, tail-biting, polys 133/171/165 octal |
| CRC | 16 bits, masked by antenna-port count -- 1, 2 or 4 ports |
| MIB | bandwidth, PHICH duration and resource, SFN |

The 40 ms period is the hard part: one PBCH transmission is 480 of the 1920
bits and does not say which quarter it is. Four descrambling offsets are tried;
the one whose CRC passes gives SFN mod 4 as well as the message.

Feeding it off the air is `lte_pbch_soft_bits()` in `lte_dsp.c`: CRS-based
channel estimate in slot 1 of subframe 0, equalise, QPSK soft demod, skipping
the reference REs.

## Answer, so far

`src/lte_mib.{c,h}`, `make check-lte-mib`, 289 checks. Every layer is pushed on
in both directions and against properties only the real construction has: the
interleaver is proved to place each coded bit sixteen times over the period and
four times in each transmission, the parity to catch every single-bit error
under all three antenna-port masks, the trellis to close on itself, and the
whole chain to decode at a noise level equal to the signal.

It does not decode a live cell. That is `05-the-broadcast-channel-on-air.md`.
