# 434 MHz 2-FSK SRD Remote-Control Protocol

This document records the measured physical layer and message structure used by
the 2-FSK SRD fixture. Payload semantics remain opaque.

## Fixture

| Property | Value |
| --- | --- |
| Capture | `testfiles/srd_remote_control_fsk.bin` |
| Sidecar | `testfiles/srd_remote_control_fsk.json` |
| Centre frequency | 433.800 MHz |
| Sample rate | 2.000 MS/s, unsigned 8-bit interleaved I/Q |
| Duration | 5.046 s |
| Signal above local floor | about 31 dB |

The fixture is committed and is required by the SRD DSP and frame checks.

## Transmission Structure

Activity occurs in trains of bursts spaced at a 100 ms interval. Each burst is
24-26 ms long, with approximately 75 ms of quiet time before the next burst.
Wake-up and data bursts alternate across two RF channels.

## Physical Layer

- Modulation: constant-envelope 2-FSK / 2-GFSK.
- Tone deviation: approximately $\pm 15.56\text{ kHz}$.
- Tone separation: approximately $31.12\text{ kHz}$.
- Chip rate: 15,625 chips/s, or $64\ \mu\text{s}$ per chip.
- Sampling geometry: 128 input samples per chip at 2 MS/s.
- Line code: Manchester, yielding 7,812.5 bit/s.
- RF channels: approximately 433.6657 MHz and 434.1857 MHz.
- Channel separation: approximately 520 kHz.

A representative ten-burst train alternates wake-up and data activity:

| Relative time | Channel | Content |
| --- | --- | --- |
| +0 ms | 1 | Wake-up / preamble |
| +100 ms | 1 | Data frame |
| +200 ms | 2 | Wake-up / preamble |
| +300 ms | 2 | Data frame |
| +400 ms | 1 | Wake-up / synchronisation |
| +500 ms | 1 | Data frame |
| +600 ms | 2 | Wake-up / synchronisation |
| +700 ms | 2 | Data frame |
| +800 ms | 1 | Data frame |
| +900 ms | 2 | Data frame |

## Message Structure

The discriminator, chip recovery, and Manchester decoder produce 20-byte data
frames with this observed structure:

| Bytes | Meaning established by the capture |
| --- | --- |
| 0-4 | Repeating preamble pattern |
| 5-7 | Synchronisation marker `06 C0 D4` |
| 8-9 | Channel and sub-burst fields |
| 10-14 | Protocol fields stable across the fixture |
| 15 | Sequence field that changes between activation trains |
| 16-19 | Opaque dynamic payload |

Corresponding retransmissions on the two RF channels carry the same opaque
payload while their channel field differs. The stable three-byte prefixes used
by `srd_device_type_of()` identify this protocol only; they do not identify a
particular product or transmitter.

## Decode Chain

1. `srd_find_transmissions()` locates bounded activity and measures its carrier.
2. `srd_classify_modulation()` distinguishes constant-envelope 2-FSK from OOK.
3. `srd_demodulate_fsk()` converts phase differences to a discriminator stream.
4. `srd_chip_period()` recovers the approximately $64\ \mu\text{s}$ chip.
5. Manchester decoding yields candidate frames.
6. `srd_extract_frames()` reports every maximal legal frame and leaves the
   payload uninterpreted.

The committed fixture is a real-signal check of this chain. Synthetic unit
fixtures remain useful for arithmetic and edge cases, but they cannot establish
the on-air modulation convention by themselves.