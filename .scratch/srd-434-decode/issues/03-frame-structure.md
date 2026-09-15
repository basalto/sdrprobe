# What a frame is: bits, boundaries and the 750 us delimiter

Status: done

## What is known

Within one activity group, reproducing across frame repetitions:

- a preamble that is a clean 1 kHz square wave, 500 us high / 500 us low;
- a data section of mixed 500 and 1000 us runs, about 170 ms;
- **six alternations at 750 us** -- 1.5 chips, a length appearing nowhere else
  in the transmission -- acting as a delimiter;
- the whole pattern repeated several times across a press of 849-1103 ms.

## Answer: What the measurements established across all 7 presses

### 1. Frame length and fields

Every transmission transmits two distinct frame types, bracketed by the 750 µs delimiter:

1. **Full Frame (`SRD_FRAME_FULL`)**: **80 bits (10 bytes = 160 chips = 80 ms)**.
   - **Byte 0 (Header / Format)**: `0x3F` (G.E. Thomas) / `0xC0` (IEEE 802.3).
     Fixed across all 7 presses. In Thomas binary: `00111111`.
   - **Bytes 1..8 (64-bit Opaque Payload)**:
     Dynamic protocol data whose semantics are not interpreted.
   - **Byte 9 (Trailer / Framing End)**: `0xD4` (Thomas) / `0x2B` (IEEE).
     In Thomas binary: `11010100`. Fixed across all 7 presses and every frame repetition.
   - Repetitions inside one activity group carry identical opaque payloads;
     separate activity groups carry different opaque payloads.

2. **Repeat / Keepalive Frame (`SRD_FRAME_REPEAT`)**: **24 bits (3 bytes = 48 chips = 24 ms)**.
   - Transmitted when the button is held down past ~900 ms (observed in Capt A presses 3 & 4, and Capt B presses 2 & 3).
   - **Byte 0**: `0x1F` (Thomas) / `0xE0` (IEEE). Differs from full frame `0x3F` by bit 2 (`00011111` vs `00111111`).
  - **Byte 1**: Tag matching Byte 1 of the preceding full frame.
   - **Byte 2**: Trailer `0xD4` (Thomas).

### 2. Boundaries and the 750 µs delimiter

- **Delimiter**: Exactly 6 alternations of ~750 µs (1.5 chips = 4.5 ms total):
  `H 750 us, L 750 us, H 750 us, L 750 us, H 750 us, L 750 us`.
  1.5 chips is a deliberate Manchester violation that can never occur during valid Manchester data.
  It acts as an unambiguous physical-layer frame sync marker.
- **Preamble**: Preceding the first delimiter is ~125 ms of alternating 500 µs square wave, ending in `H 1000 us, L 500 us` immediately before `H 750 us`.
- **Data start**: Exactly on the run following the 6th delimiter run (run index `delimiter + 6`).
- **Data end**: 80 bits (160 chips) later, followed immediately by ~64 ms of square-wave preamble and the next delimiter.
- **Repetition**: A standard ~850 ms press transmits the 80-bit full frame **4 times identically**, with 0 errors on every repetition.

### 3. Checksum

Byte 9 is `0xD4` while bytes 1..8 vary. Therefore byte 9 is a fixed
framing trailer, not an external CRC-8 over bytes 0..8. No integrity-field
semantics are inferred.

### 4. Implementation

- `src/srd_frame.{c,h}`: extracts `SRD_FRAME_FULL` (80-bit) and `SRD_FRAME_REPEAT` (24-bit) frames by scanning for 750 µs delimiters, converting to chips, and decoding.
- `tests/srd_frame_test.c`: unit checks on synthetic full and repeat frames, plus real-capture invariant assertions across `testfiles/srd_remote_control_ook_a.bin` (29 checks ok).
- Wired into `Makefile` (`check-srd-frame` in `CHECK_UNITS`, `make check`), documented in `AGENTS.md` and `CLAUDE.md`.
