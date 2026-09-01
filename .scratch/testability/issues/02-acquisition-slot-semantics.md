# 02 — The block slot: overwrite, lossless handoff, and shutdown

Status: ready-for-agent
Blocked by: (none)

`publish_block`/`consume_latest` in `src/acquisition.c` decide whether a block
is seen or dropped, and the file now carries two modes: the overwriteable slot
of ADR-0002, and the lossless handoff added for scripted playback. Nothing
checks either directly. `check-pipelines` asserts the *consequence* (a capture
decodes the same messages twice) which is the important half, but it cannot
distinguish "the slot is correct" from "this machine happened to keep up".

The counters are part of the contract and are read by the header display:
`published_blocks`, `processed_blocks`, `overwritten_blocks`,
`malformed_blocks`. An operator uses `overwritten_blocks` to decide whether to
believe what they are looking at, so a counter that lies is worse than no
counter.

Untested cases with teeth: a block longer than `SAMPLE_BLOCK_BYTES` and a block
of odd length (both counted malformed, the odd one truncated to a whole pair);
`generation` not repeating, so the same block is never processed twice; a
lossless publisher woken by `request_worker_stop` rather than by a consumer —
without the broadcast this is a hang at `pthread_join`, and a hang in a check
script is a 90-second timeout rather than a message.

What it would take: acquisition already avoids `struct app`, so a test can link
`acquisition.c` directly. It pulls in librtlsdr for the type only; if that is
awkward, the slot can move to its own file. Drive `publish_block` from a thread
and `consume_latest` from the test, and assert the counters and the stop path.

## Comments
