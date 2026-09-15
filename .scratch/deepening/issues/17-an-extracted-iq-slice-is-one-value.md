# 17 - An extracted IQ slice is one value

Status: done

## What to build

Make one extracted IQ-ring slice carry its owned bytes and complete sample
metadata as one coherent value, then use that value from retrospective
analysis and slice persistence.

The current extraction interface requires a caller-sized byte buffer and a
row of output parameters for byte count, rate, frequency, format and full
scale. The waterfall report guesses its capacity from two seconds at 2 MS/s
and the widest current sample container even though the device profile admits
higher rates. The ring knows the exact stored extent and metadata, while the
caller reconstructs the object they describe.

Extraction should own allocation from the requested duration and return one
snapshot whose bytes cannot be separated from the metadata under which they
were captured. Persistence remains an adapter over the snapshot and keeps file
naming, JSON spelling and filesystem policy.

Do not design this from future UHD semantics. Ticket 16 supplies the second
real consumer and should establish which metadata analysis actually needs.

## Why this is deep

The interface hides ring layout, byte-capacity arithmetic, wraparound copying
and metadata coherence. Deleting it would force each consumer to recover those
details. It remains a medium-strength opportunity until ticket 16 proves the
consumer shape, which is why this ticket is blocked.

## Cheapest discriminating check

Configure the ring at a rate above 2 MS/s with the widest supported container,
push enough wrapped data for a two-second request, and extract it without a
caller capacity. The snapshot must contain the requested duration when the
ring has it, exact bytes across wraparound, and the metadata active when those
bytes were stored.

## Acceptance criteria

- [x] Extraction returns one owned snapshot containing bytes, byte/pair count,
      timing, sample rate, format, full scale, tuning and source metadata.
- [x] Callers do not guess a maximum byte capacity or assemble parallel output
      parameters.
- [x] Snapshot lifetime and cleanup are explicit and covered by the check.
- [x] Wraparound, partial-history and unavailable-history behavior remain
      deterministic.
- [x] Reconfiguration cannot pair old bytes with new metadata.
- [x] Retrospective analysis consumes the snapshot without consulting the
      application's current device state.
- [x] Slice persistence consumes the same snapshot while retaining sidecar and
      filesystem policy outside the ring.
- [x] Existing IQ-ring and capture-sidecar behavior remains unchanged.

## Blocked by

- Ticket 16 - Retrospective signal analysis outside the overlay.

## Not in scope

- Designing UHD timestamps, overflow handling or stream semantics.
- Changing the acquisition slot from ADR-0002.
- Normalizing sample counts or changing capture formats.
- Introducing a generic persistence interface for one file format.

## Comments

Implemented **inside `src/iq_ring.{c,h}`**, not as a module of its own:
`struct iq_snapshot` (`src/iq_ring.h:45`), `iq_ring_extract_snapshot()`,
`iq_snapshot_free()` and `iq_snapshot_save()`. Covered by
`tests/iq_ring_test.c` under `check-iq-ring`; consumed by
`src/overlay_signal_report.c`. All checks pass.

An earlier version of this line claimed `src/iq_snapshot.{c,h}`,
`tests/iq_snapshot_test.c` and a `check-iq-snapshot`. **None of those exist**
and none ever did -- the snapshot went into the ring that already knew the
extent and the metadata, which is where the ticket's own argument points. The
wrong record is worth keeping visible because it is the failure this tracker
exists to prevent: a closed ticket that sends the next reader looking for a
module nobody built.

One loose end deliberately left: `iq_ring_extract_slice()` (`src/iq_ring.c:133`)
is the caller-sized-buffer form this ticket replaced, is marked deprecated in
the header, and has **no caller in `src/`** -- only `tests/iq_ring_test.c:72`.
It still compiles and is still checked.
