# 02 — Auto-save undecoded signals to staging folder

Status: done

Closed 2026-09-15 as already built, in `update_srd()` at `src/view_srd.c:158`.

- `captures/staging/`, created on demand, gitignored with the rest of
  `captures/`.
- `app->srd.auto_save_staging`, toggled from the SRD header's Auto-save button
  (`srd_layout.h`'s `auto_save_button`, `view_srd.c:342`).
- Fires on a `srd_session_undecoded_event` -- a burst the detector found and
  frame extraction could not read -- and writes
  `captures/staging/undecoded_YYYYMMDD-HHMMSS.bin` with a sidecar through
  `iq_ring_save_slice()`.
- Throttled to one save per second (`last_auto_save_time`), and the slice is
  the burst's own extent plus 100 ms of padding.

The session/adapter split this ended up with is
`.scratch/deepening/issues/15-one-srd-decode-session.md`'s: the session decides
that a burst went undecoded, and the staging path, the wall-clock filename and
the ring extraction are adapter work triggered by that event.

## What

When decoding bursty / intermittent signals (e.g. in the SRD or other decoder
views), unrecognised or undecodable bursts are often lost after the display
scrolls by. Providing an auto-save staging option captures these signals to disk
so they can be analysed and added to test fixtures offline.

## Specification

- Folder: `captures/staging/` (gitignored).
- Setting: `auto_save_staging` boolean toggle in `struct app` and UI.
- Trigger: When a transmission is detected by the burst detector, but frame
  extraction yields 0 frames or modulation cannot be classified.
- Action: Extract the burst slice (with pre/post padding) from the IQ ring buffer
  and write `captures/staging/undecoded_YYYYMMDD-HHMMSS.bin` with sidecar.
- Throttling: Rate limit to avoid flooding disk during sustained noise.
