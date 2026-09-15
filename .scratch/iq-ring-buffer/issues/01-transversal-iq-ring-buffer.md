# 01 — Transversal IQ history ring buffer

Status: done

Closed 2026-09-15 as already built. The work landed under
`.scratch/deepening/issues/17-an-extracted-iq-slice-is-one-value.md`, which
took the interface further than this ticket specified: `iq_ring_extract_slice()`
survives as the deprecated caller-allocated form and
`iq_ring_extract_snapshot()` returns one owned value carrying its bytes and the
metadata they were captured under, so the two cannot be separated.

- `src/iq_ring.{c,h}` -- `IQ_RING_DEFAULT_SECONDS` 30.0, sized from
  `sample_rate * duration * bytes_per_pair` by `iq_ring_configure()`.
- Pushed from `acquisition.c:157`, configured at `acquisition.c:572` and
  re-configured on retune from `sdrprobe.c:532` and `:638`.
- `iq_snapshot_save()` / `iq_ring_save_slice()` write the `.bin` and its sidecar.
- `tests/iq_ring_test.c`, gated in `CHECK_UNITS` as `check-iq-ring`: four tests
  covering init/configure, push and extract, save, and snapshot extraction
  across a reconfiguration.

## What

Maintain a continuous rolling buffer of raw I/Q samples in memory during receiver
acquisition. This enables looking backward in time to extract past signals,
transmissions, and bursts displayed on charts.

## Specification

- `src/iq_ring.h` and `src/iq_ring.c`:
  - Fixed capacity of 30.0 seconds at the current sample rate.
  - Dynamically sized or allocated to hold `sample_rate * 30 * bytes_per_pair`.
  - Push path called on each block in `publish_block()` or block consumption.
  - Tracks monotonic timestamps, sample indices, and hardware metadata.
  - Slice extraction: `iq_ring_extract_slice()` retrieves a sample window
    around a specified past time `now - age`.
  - File saving: `iq_ring_save_slice()` writes raw I/Q `.bin` and `.json` sidecar.
- Unit test in `tests/iq_ring_test.c` checking wrap-around, timestamp lookup, and
  slice bounds.
