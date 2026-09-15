# 02 - Recording produces the artifact

Status: ready-for-agent

## What to build

Make every capture recorded by sdrprobe automatically produce the waterfall
artifact from ticket 01 after its raw file is complete. Automatic duration,
manual early stop, GUI recording and headless recording must all converge on
one finalization path.

Acquisition continues to tee raw bytes without waiting for image work. Closing
the `.bin` publishes a completed-capture value to the application adapter;
generation reads that immutable capture after the recording mutex has been
released. The final sidecar references the image only after successful export.

This is one capture-finalization transaction, not a notification added after
the two existing completion branches. It closes and freezes the Capture once,
then sidecar, waterfall and GUI/headless status adapters consume that completed
value outside acquisition locks.

## Implementation plan

1. Represent recording completion as one value containing the capture path and
   the metadata snapshotted when recording began.
2. Make both the byte-limit and explicit-stop paths close and publish that
   value exactly once, without running replay, rasterization or PNG encoding
   under an acquisition lock or callback.
3. Have windowed and headless adapters consume the same completion and invoke
   ticket 01's generator.
4. Define shutdown behavior: an in-progress artifact generation completes or
   fails visibly before the process exits; it is never silently abandoned.
5. Exercise the assembled path over deterministic file playback and inspect
   the resulting capture set.

## Tasks

- [ ] Add a one-shot recording-completed event/value with explicit ownership
      and cleanup.
- [ ] Route normal limit completion and early stop through the same event.
- [ ] Remove duplicated close/sidecar ordering from the byte-limit and explicit
      stop branches.
- [ ] Close and flush the raw capture before any consumer can generate its
      image.
- [ ] Invoke one artifact generator from GUI and headless recording paths.
- [ ] Report image-generation failure separately from raw-capture success.
- [ ] Ensure a second recording cannot consume or overwrite the prior
      completion event.
- [ ] Extend acquisition and pipeline checks for success, early stop, export
      failure and shutdown during finalization.
- [ ] Document the three-file capture set and failure behavior.

## Acceptance criteria

- [ ] Every successful GUI or headless recording leaves sibling `.bin`,
      `.json` and `.png` files.
- [ ] Both automatic completion and early stop produce one image and one
      sidecar reference.
- [ ] The sidecar path is relative and resolves from the sidecar directory.
- [ ] No image replay or encoding occurs while the recording mutex is held or
      in the receiver's block-publish callback.
- [ ] A failed PNG export preserves the raw capture and valid sidecar, omits
      the image field and reports the failure.
- [ ] Process shutdown waits for or explicitly cancels finalization without a
      dangling sidecar reference.
- [ ] A file-playback pipeline check proves the recorded bytes retain their
      existing decode result and the generated PNG is valid.

## Blocked by

- Ticket 01 - Existing capture to waterfall artifact.

## Not in scope

- Capturing screenshots of arbitrary views.
- Background regeneration of historical captures.
- Uploading capture artifacts or serving them over a network.

## Comments

Updated from the 2026-09-15 architecture review. The deletion test is
positive: without the finalization module, close ordering, metadata ownership
and failure handling return to both completion branches and every output
adapter. Report: `/tmp/architecture-review-20260915-175714.html`.
