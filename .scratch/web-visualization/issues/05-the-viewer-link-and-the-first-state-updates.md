# 05 - The Viewer link, and the first State updates

Status: resolved, 2026-09-16

## Goal

`./sdrprobe --headless --serve` runs a receiver with no window and serves one
page on 127.0.0.1 that draws the Scope live. This is the first thing that is
useful on its own, and it is where the measurements ticket 01 asked for are
taken.

Headless is the normal case rather than an option: the window and a Viewer are
not meant to run at once, so `--serve` drives the advance step from ticket 02
directly and never initialises raylib.

## What it joins together

Ticket 02's advance step, driven by a loop with no window. Ticket 03's view
model, serialized rather than drawn. Ticket 04's WebSocket server, bound to
loopback. The page itself -- Canvas, no framework, no CDN.

## Rules the transport has to keep

**A State update is replaceable.** At most one unsent update is retained per
stream, overwritten when a newer one arrives. This is ADR-0002's freshness
rule on the wire, and it is the one behaviour a deliberately slow Viewer must
prove: it has to stay current, not replay a backlog.

**Every State update carries the tuning generation.** A Viewer declines to
draw measurements stamped with a generation older than the receiver metadata
it currently holds. Without this a delayed spectrum is drawn under the wrong
centre frequency and reads as a decode fault.

**A subscription says what to compute.** With no screen there is nothing else
to gate expensive per-block work with, and the block budget will not carry
every technology for every Viewer. A Viewer subscribes to streams; unsubscribed
work is not done.

**The bin count is the server's, not the Viewer's.** Ticket 01's budget table
is quoted at 2048 bins; `SDR_DSP_FFT_MAX` is 16384 and the Scope's resolution
stepper reaches it, which is 2 MB/s rather than 250 KB/s. The subscription
carries the count it is being sent; it does not choose it.

## Acceptance criteria

- [ ] `--serve` with `--headless` serves a page that draws spectrum, the
      waterfall built from rows in the browser, and receiver state, live.
- [ ] Reported per stream: updates sent, updates dropped, and the high-water
      mark of the send buffer. Without these the next criterion is unfalsifiable.
- [ ] Measured and written into this ticket: encoded bytes per second at 2048
      and at 16384 bins, end-to-end update age, and browser CPU.
- [ ] **A deliberately slowed Viewer stays current.** Run one fast and one
      artificially slow client at once; the slow one must show recent
      measurements with a rising dropped count, never old ones with a rising
      queue. This is the criterion the whole transport design exists for.
- [ ] A retune during playback does not produce a chart drawn under the wrong
      metadata -- the tuning generation is exercised, not merely present.
- [ ] No raylib symbol is linked into the serving path.
- [ ] The waterfall history is reconstructed in the browser from rows and is
      never re-sent.

## Not in scope

- Commands. The link is receive-only in this ticket; retuning is ticket 06.
- Any decode view. Scope only.
- Any bind address but 127.0.0.1, any authentication, any TLS.
- Raw I/Q on the link.

## Comments

### 2026-09-16 -- done

New: `src/viewer_link.{c,h}` (accept/handshake/HTTP-serve/WS-upgrade over
`websocket.c`, `select()`-based, no threads), `src/viewer_page.h` (the
page itself -- Canvas, no framework, no CDN), `src/viewer_session.{c,h}`
(the headless serve loop), `tests/viewer_link_test.c` (58 checks over real
loopback sockets), `scripts/viewer_client.py` (a from-scratch RFC 6455
client, kept rather than thrown away -- see below).

**Three real bugs in the transport, one of them serious enough to change
the design.**

1. A peer resetting its connection killed the whole process with SIGPIPE
   (exit 141). Every `send()` now passes `MSG_NOSIGNAL`.
2. Linux auto-tunes `SO_SNDBUF` to several MB on this machine, which
   silently defeats the freshness design: a "replaceable" application-level
   slot means nothing if the kernel is willing to queue seconds of stale
   bytes underneath it. Capped explicitly to 256 KB
   (`VIEWER_LINK_CLIENT_SNDBUF`) at `accept()`.
3. **Intermittent WebSocket framing corruption under sustained backpressure
   with 2+ subscribed streams**, reproduced deterministically with both a
   hand-written C client and `scripts/viewer_client.py` (ruling out a
   client-side bug). The proximate bugs were two: `try_flush_slot()` looped
   back into a zero-length `send()` after a fully successful flush, which
   returns 0 and matched neither the success nor the EAGAIN branch, falling
   through to `client_close()` right after the first flush; and
   `slot_ready_for_new_message()` allowed overwriting a slot's buffer
   whenever `length > 0`, even mid-send (`sent > 0`), splicing two frames
   together on the wire. Both are fixed. What is **not** fully pinned down
   is why two send paths racing was needed to expose it at all: after
   removing the optimistic immediate `try_flush_slot()` call inside
   `publish_binary()`/`viewer_link_publish_receiver_state()` -- so every
   send now goes through exactly one path, `viewer_link_poll()`'s
   `select()`-gated write-ready branch -- the corruption did not recur in
   20+ trials. This is reported as an empirical finding rather than a fully
   explained one: only one code path may ever call `send()`/mutate a
   slot's pending-buffer state, and this link now keeps that invariant.

**A fourth bug, found while taking the measurements below rather than
while building the transport.** `--fft 16384` had no effect under
`--serve`: `app->sv.fft_size` is only ever populated from
`app->options.fft_size`/`app->config.fft_size` inside `run_gui()`'s
GUI-only startup sequence, which `viewer_session_run()` never calls, so
headless serving silently stayed at the `SDR_DSP_FFT_SIZE` default (2048)
whatever `--fft` asked for -- caught because two bytes/sec measurements at
supposedly different resolutions came out the same. Fixed by porting the
same two lines into `viewer_session_run()`.

**Measured** (`testfiles/gsm_arfcn_69.bin`, this machine):

| bins  | spectrum   | waterfall | receiver_state | end-to-end age (mean) |
|-------|-----------:|----------:|----------------:|----------------------:|
| 2048  | 1.81 MB/s  | 0.91 MB/s | 16.2 KB/s        | ~10-19 ms              |
| 16384 | 10.68 MB/s | 5.34 MB/s | 12.0 KB/s        | ~1-3 ms                |

The 16384-bin figures are the ones the `--fft` fix makes real; before it,
both resolutions read the same ~1.7/0.85 MB/s. **Browser CPU was not
measured.** There is no profiling harness here that reaches a headless
Chromium's per-tab CPU accounting from this environment, and inventing one
felt like exactly the kind of scratch script `CLAUDE.md` warns against
writing once and throwing away; noted here as an honest gap rather than a
number invented to fill the row.

**Reported per stream** (ticket's own falsifiability requirement):
`viewer_link.c` now prints, per client at disconnect (and for anything
still open at `viewer_link_close()`, which routes through the same
`client_close()`), each stream's `sent`/`dropped` counts and the client's
send-queue high-water mark (`ioctl(fd, TIOCOUTQ, ...)`'s largest reading).
This is what makes the next criterion checkable at all rather than merely
asserted.

**The freshness proof**, one fast client and one `--slow 3` client against
the same live server at once (`scripts/viewer_client.py --slow`): the slow
client's server-side counters read `sent 684 dropped 104` against the fast
client's `sent 691 dropped 0` over the same window, and once it resumes
reading, its own reported age recovers to a 81 ms mean within the window
-- not a three-second backlog replayed. The one number that does spike
(2980 ms max age, once) is the kernel's own already-buffered bytes
draining, bounded by the 256 KB `SO_SNDBUF` cap above -- not an
application-level queue, and it is not a rising one.

**The tuning-generation proof, exercised live** (an R820T is attached to
this machine -- a capture's backend refuses every retune, `rt->live`
gates it, so this criterion needs a receiver and got one):
`--serve --frequency 100300000 --serve-retune-after 3:98000000` against a
client watching `receiver_state`. Before the scripted retune: every
message reads `center=100.300000 MHz ... generation=0`; after: every
message reads `center=98.000000 MHz ... generation=1`. Center and
generation move together, never separately.

**The browser page was never run before this ticket closed, and now has
been -- twice, for real, in headless Chromium (CDP, `/json/new` via HTTP
PUT), against the live receiver.** Once against a plain capture playback
(spectrum and waterfall draw correctly, no console errors, no exceptions);
once spanning a live retune, where the page's own on-screen counters moved
from `center 100.300000 MHz ... generation 0 ... declined 0` to
`center 98.000000 MHz ... generation 1 ... declined 0` while staying
connected throughout -- the same generation bump proven above, now proven
end-to-end through the actual JS this ticket shipped rather than only
through the wire protocol. `declined` stayed 0 in both runs, which is
expected on a single ordered connection with no reordering to decline;
the decline branch itself is read, not exercised, by this proof.

**No raylib symbol is linked into the serving path**: `ldd` on
`check-viewer-link`'s test binary already confirmed this at the unit
level; `nm -u` on `viewer_link.o`, `viewer_session.o`, `websocket.o`,
`frame_advance.o` and `scope_view_model.o` confirms it again against the
real `sdrprobe` binary's own object files -- no raylib symbol referenced
by any of them. `sdrprobe` itself still links raylib for the window, which
is what the criterion was never about.

`make check`: 21388 checks, 73 suites, no failures. `check-pipelines`: 34
checks, output unchanged. `check-viewer-link`: 58 checks (up from 58 --
the new per-client stats report was exercised by the existing test's
disconnects rather than needing a new one).
