# 434 MHz OOK SRD Remote-Control Protocol

This document records the measured OOK physical layer, framing, and regression
coverage of the committed SRD fixture. The 64-bit message body is treated
as opaque data.

## Fixtures

| Capture | Tuning | Activity groups | Duration |
| --- | --- | --- | --- |
| `testfiles/srd_remote_control_ook_a.bin` | 433.800 MHz | 4 | 5.046 s |

The capture is part of the external corpus. `check-srd-dsp`, `check-srd-frame`,
and `check-pipelines` use it when present and report a skip otherwise.

## Physical Layer

- Absolute carrier: approximately 434.417 MHz.
- Modulation: on-off keying.
- Chip period: approximately $500\ \mu\text{s}$.
- Line code: Manchester.
- Delimiter: six alternating runs of approximately $750\ \mu\text{s}$.
- Full frame: 80 bits / 10 bytes.
- Repeat frame: 24 bits / 3 bytes.

The tuning places the carrier approximately +617 kHz from receiver DC.

## Message Structure

### Full frame

| Byte | Meaning |
| --- | --- |
| 0 | Full-frame header: `0x3F` in Thomas polarity, `0xC0` in IEEE polarity |
| 1-8 | Opaque 64-bit payload |
| 9 | Trailer: `0xD4` in Thomas polarity, `0x2B` in IEEE polarity |

### Repeat frame

| Byte | Meaning |
| --- | --- |
| 0 | Repeat header: `0x1F` in Thomas polarity, `0xE0` in IEEE polarity |
| 1 | Tag matching byte 1 of the preceding full frame |
| 2 | Trailer: `0xD4` in Thomas polarity, `0x2B` in IEEE polarity |

These relationships establish frame boundaries and message type. They do not
assign meaning to the opaque payload.

## Decode Chain

1. `srd_find_transmissions()` scans the complete capture for bounded activity.
2. `signal_find_activity()` chooses a busy window before expensive measurement.
3. `signal_find_carrier()` refines the carrier inside that window.
4. `srd_demodulate_envelope()` mixes and filters one transmission.
5. `srd_extract_runs()` and `srd_chip_period()` recover the run and chip grid.
6. `srd_manchester_decode_both()` checks both Manchester polarities.
7. `srd_extract_frames()` validates header, length, and trailer.

The activity-window step is load-bearing. A fixed prefix can contain only
silence when a short transmission occurs later in the capture, producing a
confident absence unless the whole file is first searched for activity.

## Regression Coverage

`check-pipelines` surveys both tunings and requires them to place the carrier
near 434.417 MHz and within 10 kHz of each other. It then drives the assembled
headless SRD decode over the OOK fixture and requires multiple decoded frames
with the known protocol header and trailer.

`check-srd-dsp` pins activity count, carrier placement, level above the local
floor, chip recovery, and Manchester quality. `check-srd-frame` pins the full
and repeat frame structure against both synthetic vectors and the committed
capture.

## Diagnostic Commands

```sh
make probe-ook FILE_OOK=testfiles/srd_remote_control_ook_a.bin
make probe-srd FILE_SRD=testfiles/srd_remote_control_ook_a.bin
./sdrprobe headless --file testfiles/srd_remote_control_ook_a.bin \
    --technology srd --decode --once
```

The diagnostics report measured carrier, activity windows, runs, chip period,
Manchester violations, and frames. Payload bytes are displayed as uninterpreted
protocol data.