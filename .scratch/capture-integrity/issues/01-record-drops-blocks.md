# 01 — Record writes the renderer's blocks, so it silently drops samples

Status: resolved

`record_block()` (`src/sdrprobe.c`) is called from the frame loop under
`if (have_new)`, and writes `app->raw` — the block the *renderer* consumed:

```c
if (have_new)
    record_block(app);
```

But acquisition hands blocks over through a single overwriteable slot
(ADR-0002). `publish_block()` counts and discards whatever the renderer has not
picked up yet:

```c
if (latest->ready)
    latest->overwritten_blocks++;
memcpy(latest->data, data, valid_len);
```

So every overwritten block is **missing from the recording**, with no gap
marker and nothing in the file to say it happened. Dropping blocks is correct
for a display — that is exactly what ADR-0002 chose, and why — but a capture
inherits the behaviour without inheriting the justification.

At 2 MS/s a block is 65.5 ms and the frame loop targets 60 FPS, so there is
normally ~4 frames of slack. The exposure is that the Record button lives in
the GSM decode view, whose per-block work is the most expensive in the app: a
full SCH search (8 timings x ~17600 positions x 63-bit correlation) plus the
64-state trellis, every block. One slow frame silently splices the capture.

**This did not damage `testfiles/gsm_arfcn_69.bin`.** Verified: all 42 gaps
between detected SCH bursts are exactly 10 or 11 frames, in the regular
`10 10 10 10 11` multiframe pattern. A dropped block would show up as a ~24
frame gap, and there is none. So that fixture is contiguous — but any future
capture is a coin flip, and nothing in the file would reveal it.

## Fix

Record from the publish side rather than the consume side, so the capture is
driven by acquisition rather than by frame rate. The minimum acceptable
alternative is to snapshot `overwritten_blocks` when recording starts and
compare when it ends, then refuse to keep — or clearly mark — a capture that
lost blocks. A silently spliced test vector is worse than no test vector: it
invalidates any timeline analysis done against it, without ever looking wrong.

Worth pairing with the metadata sidecar already on `TODO.md` ("raw I/Q
recording with center frequency, sample rate, gain, PPM, tuner identity, and
timestamps as metadata") — the same write path, and a drop count belongs in
exactly that record.


## Resolved 2026-08-30

`record_capture()` now writes from the acquisition thread, called at the end of
`publish_block()` once the slot mutex is released, so the capture is driven by
acquisition rather than by frame rate. `record_block()` and its `if (have_new)`
call site are gone.

- The recording fields moved under a dedicated `record_mutex`, since the
  acquisition thread now owns them. `start_record()` does the `fopen` outside
  the lock and publishes the open file under it; the UI reads through
  `record_snapshot()`. The two mutexes are never held together.
- A block that is not the full `SAMPLE_BLOCK_BYTES` is counted, and the
  completion message says outright that the capture is not contiguous. A short
  write reports as truncated. Stopping mid-recording (quit, or a failed start)
  reports how much was written.
- The mutex is created before any path that can reach `cleanup`, and destroyed
  after the worker is joined.

Verified with a throwaway harness: two builds, pre-fix and post-fix, each
patched to auto-record and to run the frame loop at 100 ms — deliberately
slower than the 65.5 ms block rate, which is the condition that triggers the
drop. Both recorded 31 blocks from `--file` playback of
`testfiles/gsm_arfcn_69.bin`:

```
old: 8126464 bytes -- diverges from the source at byte 1 (spliced from the start)
new: 8126464 bytes -- byte-identical to the source prefix (contiguous)
```

Note ADR-0002 already stated that the single-slot handoff "is *not* suitable
for lossless capture"; the Record button was built on it regardless. The ADR's
consequences now record where capture actually tees off.

**Done (2026-08-30).** The metadata sidecar from `TODO.md` now rides the same
write path. `record_write_sidecar()` writes `<capture>.json` when the recording
closes, on both the normal and the stopped-early path: centre frequency, sample
rate, gain, PPM, tuner chip, source, start time, duration, byte count, and —
where the GSM view had selected a channel — the ARFCN and the carrier's offset
from the recorded centre. That last field is the one that mattered most: the
"tuned 400 kHz below the channel" convention for `gsm_arfcn_69.bin` lived only
in prose, and everything in `.scratch/sch-frame-number/` depended on knowing it.

The short-block count is in there too, so a capture states its own contiguity
instead of leaving it to be discovered — which is the number this ticket's fix
made meaningful in the first place.

The tuning is snapshotted on the main thread when recording starts, so the
acquisition thread that writes the file never reads live state.
