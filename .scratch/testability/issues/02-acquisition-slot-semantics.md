# 02 — The block slot: overwrite, lossless handoff, and shutdown

Status: resolved
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

## Answer

Done in `tests/acquisition_test.c` (`make check-acquisition`, 52 checks). No
new module was needed: acquisition already avoids `struct app`, so the check
links `acquisition.c` directly and pulls in librtlsdr for the device type
without ever opening one.

One change to the code to make that possible, and worth making anyway:
`acquisition_init()` now creates all three synchronisation objects -- the
recording mutex, the slot mutex, and the condition a lossless publisher waits
on -- and `acquisition_destroy()` tears them down and returns the first error.
They were split between this function and a block in `main()` several steps
later in the startup sequence, so setting up an acquisition meant replicating
that order. `struct app` loses `record_mutex_ready`; `acq.mutex_ready` covers
it.

Checked: the slot overwrites and hands back the *newest* block, with the
overwrite counted; a block is consumed exactly once, which is what the
generation is for; an empty, oversized, odd-length or one-byte block, and the
malformed counter each one increments; publishing after a stop; the lossless
handoff delivering sixteen blocks in order with nothing overwritten; and the
file worker reading `adsb_cpr_pair.bin` whole -- every byte, once, with a clean
end.

The one that matters most is `test_stop_wakes_a_waiting_publisher`. Its failure
mode is a hang, so it bounds its own wait with `pthread_timedjoin_np` and
reports before `_exit` rather than blocking the suite. Removing the broadcast
from `request_worker_stop()` fails it in five seconds instead of hanging the
program on exit with no message.
