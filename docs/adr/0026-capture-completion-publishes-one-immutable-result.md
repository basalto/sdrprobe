# Capture completion publishes one immutable result

## Status

accepted

## Context and decision

Raw recording currently reaches completion through both the byte-limit path
and an explicit stop. Each path closes files and writes metadata, so ordering,
ownership, and failure handling can drift as more outputs are added. The
capture-waterfall work adds a third durable artifact whose generation must not
delay receiver callbacks or run while acquisition owns its recording lock.

Capture completion is therefore one transaction that closes and freezes the
raw Capture exactly once, then publishes one immutable completion result. All
completion triggers converge on that transaction. Sidecar persistence,
waterfall generation, and GUI or headless status are adapters over the same
result and run after recording ownership has been released.

The completion result contains the capture path and the recording metadata
snapshotted when recording began. Publication means the raw file has been
flushed and closed and will not receive more samples. Artifact failures are
reported independently: they do not invalidate or delete a successfully
completed raw Capture, and metadata must not claim an artifact that was not
successfully written.

## Considered options

- **Add artifact work to each existing completion branch** is locally small
  but duplicates close ordering and lets automatic completion, early stop,
  GUI, and headless recording acquire different semantics.
- **Generate artifacts while holding the recording lock** makes completion
  appear atomic but puts replay and image encoding on an acquisition-owned
  path, risking delayed block delivery and shutdown.
- **Publish a mutable acquisition record** avoids a snapshot but lets later
  recording state alter the meaning of a Capture while adapters are consuming
  it.

## Consequences

- Acquisition owns writing and closing raw samples, but does not own PNG
  rendering, sidecar formatting, or presentation status.
- A completion result is emitted at most once for a recording and cannot be
  overwritten by a subsequent recording before its consumers finish.
- Automatic limit completion, explicit stop, GUI recording, and headless
  recording share the same close and publication semantics.
- Shutdown must finish or explicitly cancel completion consumers; it may not
  silently abandon them or leave metadata pointing at absent artifacts.
- Adding another capture artifact means adding another adapter over the
  completed result, not another acquisition completion path.