# Latest-block single-slot handoff between acquisition and rendering

## Status

accepted

## Context and decision

`sdrprobe` acquires 256 KB sample blocks on a worker thread (the librtlsdr
async callback or the file-playback pacer) and renders on the main thread. The
handoff is **one mutex-guarded, overwriteable slot** — the most recent block —
not a FIFO/ring buffer. If the renderer has not consumed the previous block,
`publish_block` overwrites it and increments a dropped-block counter;
`consume_latest` copies a block only when its generation differs from the last
one it read (see `struct latest_block`, `publish_block`, `consume_latest` in
`src/sdrprobe.c`).

## Considered options

- **FIFO / ring buffer** preserving every block — rejected.

## Consequences

- A live visualizer values **freshness over continuity**: showing the newest
  data matters more than never dropping a block, so replacing stale unread data
  is intentional.
- Memory is bounded to a single slot, and the acquisition callback never blocks
  on the renderer (no backpressure into librtlsdr).
- Dropped blocks are **counted, not queued**, so the HUD can surface that the
  renderer is falling behind.
- This suits display; it is *not* suitable for lossless capture or decoding,
  which would need a real queue.
- Raw-I/Q recording therefore does **not** read the slot. `record_capture`
  writes from the acquisition thread inside `publish_block`, upstream of the
  lossy handoff, so a capture holds every block the receiver delivered without
  adding a queue. A recording driven off `consume_latest` silently inherits the
  drops and leaves a spliced file that still looks well-formed — the failure
  this consequence warned about, and one the Record button did hit; see
  `.scratch/capture-integrity/issues/01-record-drops-blocks.md`.
