# Transversal IQ History Ring Buffer & Waterfall Signal Inspection

## What this is

A background in-memory circular buffer of raw I/Q samples maintained continuously
across all views (Scope, Decoders, Survey), enabling:
1. Retrospective capture saving (extracting any signal from the past).
2. Auto-saving undecoded or unrecognised burst signals to a staging folder.
3. Right-clicking on signals in waterfall charts to save captures or run signal
   analysis reports.

## Scope

- In-memory thread-safe circular buffer (`src/iq_ring.{c,h}`).
- Capacity: 30 seconds of full-rate samples (~120 MB at 2.0 MS/s 8-bit).
- Attached to acquisition's `publish_block()` path.
- Auto-save staging mechanism for undecoded signals in `captures/staging/`.
- Interactive context menu and signal report popup on waterfall right-click.

## Tickets

1. `01-transversal-iq-ring-buffer.md` — circular buffer architecture & acquisition integration.
2. `02-undecoded-staging-auto-save.md` — auto-saving undecoded burst signals to `captures/staging/`.
3. `03-right-click-waterfall-context-menu-and-signal-report.md` — chart right-click, signal slicing & report popup.

## Outcome

All three tickets closed 2026-09-15 as already built. The ring, the staging
auto-save and the waterfall right-click all shipped; the statuses were stale
rather than the work outstanding.

Two of them were delivered under other specs, and both went further than this
one asked. `.scratch/deepening/issues/17-*` made an extracted slice one owned
value rather than a caller-sized buffer and a row of out-parameters, and
`.scratch/deepening/issues/16-*` took the modulation verdict out of the popup
and into a presentation-free analysis over `signal_probe`, so the report's
conclusions are reachable by `check-signal-analysis` rather than living in a
static raylib function.

What this spec did not ask, and what that cost: the two surfaces ticket 03 adds
-- a context menu and a modal popup -- were never given to `struct input_state`,
so the frame loop did not know either was up and `q` quit the program from
inside the report. Fixed under `.scratch/testability/issues/09-*`.
