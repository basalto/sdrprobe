# 05 - The Viewer link, and the first State updates

Status: ready-for-agent

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
