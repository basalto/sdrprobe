# 15 - One SRD decode session

Status: done

## What to build

Move the Short Range Device decode machine out of the raylib view and form one
SRD-specific session used by both the window and headless adapters.

The current view update discovers transmissions, chooses OOK or 2-FSK
demodulation, converts samples to runs, joins runs across sample blocks,
recovers the chip period, extracts frames, suppresses repeats and emits an
undecoded 2-FSK wakeup. The headless path calls that view function and reads
its newest-first presentation log. These are Decoder decisions and persistent
decode state, not drawing.

The session should take centred I/Q plus explicit sample facts and return
events and accumulated decode state. The window keeps chart buffers, controls,
receiver leasing and log-row presentation. The headless adapter keeps stdout
formatting. Staging capture paths, wall-clock filenames and IQ-ring persistence
remain adapter work triggered by an undecoded event.

This is an SRD module, not a generic session interface. ADR-0023 requires the
technology modules to share dependency and testability constraints, not one
function shape.

## Why this is deep

Deleting the session would put cross-block run assembly, burst reset,
deduplication and undecoded-event rules back into both the window and headless
adapters. The existing five technology sessions establish the local shape,
but none supplies SRD's interface.

## Cheapest discriminating check

Feed an OOK transmission across a sample-block boundary. The session must emit
each decoded frame exactly once, while resetting its stream only after the
existing quiet-block rule. Mutating the boundary reset or deduplication count
must fail this check without raylib, a receiver or a filesystem.

Then replay the existing SRD capture invariant when the private fixture is
available and compare the headless output before and after the extraction.

## Acceptance criteria

- [x] One presentation-free SRD session owns cross-block runs, quiet/busy
      transitions, frame deduplication and decode counters.
- [x] The session emits decoded-frame and undecoded-2-FSK events without
      formatting text or writing files.
- [x] The window draws session state but does not perform the decode chain.
- [x] Headless SRD output consumes session events and no longer calls a view
      update or reads a view-owned log.
- [x] Auto-save consumes an undecoded event; capture naming and persistence
      remain outside the session.
- [x] A hardware-free check covers a transmission split across blocks, quiet
      reset, duplicate suppression and both event kinds.
- [x] Existing SRD DSP/frame checks and deterministic pipeline output remain
      unchanged.
- [x] The new check is in `CHECK_UNITS`, and all new headers are complete
      Makefile prerequisites.

## Blocked by

None - can start immediately.

## Not in scope

- Changing SRD thresholds, modulation classification or frame formats.
- Completing the 2-FSK frame decoder.
- Creating a common session vtable across technologies.
- Moving receiver leases, chart traces or output formatting into the session.

## Comments

Implemented in `src/srd_session.{c,h}` with hardware-free test suite `tests/srd_session_test.c` (`check-srd-session`). All acceptance criteria verified and passed.
