# Latest-block single-slot handoff between acquisition and rendering

## Status

accepted

## Context and decision

`rtl_raylib` acquires 256 KB sample blocks on a worker thread (the librtlsdr
async callback or the file-playback pacer) and renders on the main thread. The
handoff is **one mutex-guarded, overwriteable slot** — the most recent block —
not a FIFO/ring buffer. If the renderer has not consumed the previous block,
`publish_block` overwrites it and increments a dropped-block counter;
`consume_latest` copies a block only when its generation differs from the last
one it read (see `struct latest_block`, `publish_block`, `consume_latest` in
`src/rtl_raylib.c`).

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
  which would need a real queue. That is out of scope for these probes.
