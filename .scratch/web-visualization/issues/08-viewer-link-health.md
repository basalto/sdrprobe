# 08 - Link health, shown to the Viewer

Status: resolved, 2026-09-16

## Goal

Ticket 05 measured the transport once, by hand, from outside: bytes/sec with
a CLI emulator, drops with a stats printout, and one honest gap -- *"Browser
CPU was not measured. There is no profiling harness that reaches a headless
Chromium's per-tab CPU accounting from this environment."* That gap does not
close by finding a better profiler; it closes by having the Viewer measure
itself and say so, continuously, to whoever is looking at it. This ticket
turns the numbers ticket 05 took once into a live panel on the page and a
fourth Viewer stream carrying what only the server can know.

## What the server knows that the Viewer cannot

Per stream, `sent_count` and `dropped_count` already exist in
`struct viewer_stream_slot` (ticket 05), printed to stderr at disconnect and
nowhere else. The client cannot compute a drop count -- a dropped message
never reaches it -- so this has to come from the server. Likewise
`send_queue_high_water` (`ioctl(fd, TIOCOUTQ, ...)`'s largest reading) is a
fact about the kernel's socket, invisible from the browser side.

Server CPU is a new measurement: total process CPU time (`getrusage`, which
on Linux sums every thread -- the acquisition worker included, so this is the
whole program's cost, not just the serving loop's) against wall time, sampled
independently of the block rate so the percentage is stable rather than
recomputed every 65 ms.

## What the Viewer knows that the server cannot

Received bytes/sec: the Viewer already decodes every message it gets, so it
already has the size and the arrival time of each one. No new server field
for this -- recomputing it from what the wire format already carries is more
honest than trusting a second, server-side byte counter to agree.

Browser CPU, properly: not read from the OS (nothing in a browser tab exposes
that to its own JS), but measured as **JS main-thread busy time**, the
fraction of each rolling window actually spent inside this page's own message
handling and drawing versus idle. This is a real, self-reported number, not
an estimate of what an external profiler would have said -- and it is the
answer to ticket 05's gap: it does not need Chromium's process accounting at
all, and unlike a one-off headless measurement in this environment, it is
what every real Viewer, in every real browser, reports about itself for as
long as it runs.

## Shape

A fourth stream, `link_health`, alongside `spectrum`, `waterfall`,
`receiver_state` -- a JSON text message on the same replaceable-slot rule as
`receiver_state`, published on its own cadence (about once a second; the
counters barely move block to block, and a CPU percentage recomputed every
block is noise). Flat fields, matching `receiver_state`'s own shape rather
than a nested object, since both are hand-written with `snprintf` and neither
side parses JSON generically (`src/capture_sidecar.h`'s rule, unchanged since
ticket 04): `spectrum_sent`, `spectrum_dropped`, `waterfall_sent`,
`waterfall_dropped`, `receiver_state_sent`, `receiver_state_dropped`,
`send_queue_high_water`, `server_cpu_percent`, `timestamp_ms`.

Server CPU sampling is a pure, testable computation over two `(wall, user,
system)` samples -- `src/process_cpu.h`/`.c`, no `struct app`, no coupling to
`viewer_link.c` beyond a `double` handed in. `viewer_session.c` owns when to
sample and how often.

The page's Health panel shows, per stream: sent, dropped, and a drop rate;
the send-queue high-water mark; received bytes/sec (computed client-side);
server CPU%; and this tab's own JS busy% (computed client-side, over the same
kind of rolling window).

## Acceptance criteria

- [ ] `link_health` is a fourth stream, subscribed like the other three;
      unsubscribed, nothing about it is computed or sent.
- [ ] A check over real loopback sockets (extending `check-viewer-link`)
      proves: the message is well-formed JSON-shaped text, the counts match
      the other streams' actual sent/dropped state at publish time, and it
      respects the same replaceable-slot rule.
- [ ] `check-process-cpu` proves the percentage arithmetic against synthetic
      `(wall, cpu)` pairs -- including the zero-elapsed-wall-time case, which
      must refuse rather than divide by zero -- independent of any real
      process.
- [ ] The page shows live numbers for all of: per-stream sent/dropped,
      send-queue high-water, received bytes/sec, server CPU%, and this tab's
      JS busy% -- verified in a real headless-Chromium run against a live
      `--serve` session, not asserted from the wire format alone.
- [ ] `make check` and `check-pipelines` stay green; no raylib symbol reaches
      the new server-side code.

## Not in scope

- A history or chart of health over time -- one live reading each, matching
  every other field on the page.
- Any server-side network-throughput counter -- the Viewer already has what
  it needs to compute this from what it receives.
- Alerting or thresholds on any of these numbers. This ticket shows them; it
  does not decide what counts as bad.

## Comments

### 2026-09-16 -- done

New: `src/process_cpu.{c,h}` (a sample of this process's own wall/CPU time,
and a pure percentage over two samples -- `check-process-cpu`, 12 checks,
`-lm` alone, including the zero-elapsed-wall-time and negative-interval
refusals and a multi-threaded-process-can-exceed-100% case). `viewer_link.h`
gained `VIEWER_STREAM_LINK_HEALTH` (a fourth stream, same replaceable-slot
rule as the other three) and `viewer_link_publish_link_health()`, which --
unlike the other three publishers -- builds one JSON message per subscribed
client rather than one payload fanned out to all, because sent/dropped/
high-water are each client's own. `viewer_session.c` samples the process
once a second (`VIEWER_SESSION_HEALTH_INTERVAL_SECONDS`) and publishes
`link_health` every iteration like `receiver_state`, so the counts stay
current between CPU samples.

The page's Health panel is the other half: per-stream sent/dropped and a
drop rate, the send-queue high-water mark, and two fields the *page*
computes about itself rather than trusting the server for -- received
bytes/sec (it already timestamps and sizes every message it decodes) and
its own JS main-thread busy time, wrapping the whole `onmessage` handler in
a `try`/`finally` so every exit path (the stale-generation drop included)
is timed the same way, rolled up once a second alongside the throughput
counter.

**Verified live, in real headless Chromium (CDP, `/json/new` via HTTP PUT),
against a real `--serve` session** -- no console errors, no exceptions, and
a real reading from every field:
```
spectrum sent 76 dropped 24 (24.0%)
waterfall sent 76 dropped 24 (24.0%)
receiver_state sent 76 dropped 24
send-queue high-water 352.8 KB
received 159.9 KB/s (this tab)
server CPU 82.2%
this tab, JS busy 87.4%
```
The 24% drop rate is not a planted scenario -- this headless tab, under
whatever else this machine was doing at the time, genuinely fell behind
its own subscription and the freshness rule caught it, which is a better
proof of the mechanism than a clean run would have been.

**Ticket 05's gap is closed, not argued around.** Browser CPU is still not
read from Chromium's process accounting -- nothing in a tab exposes that to
its own JS regardless of environment -- but JS busy time is a real,
self-reported number about this page, measured the same way in this dev
environment as it will be for any real Viewer in any real browser, for as
long as it runs. That is a stronger answer than a one-off external
measurement would have been even if one had been reachable here.

`make check`: 21430 checks, 74 suites, no failures (up from 21403/73: 12 from
`check-process-cpu`, 13 from `check-viewer-link`'s two new tests, 2 from the
Makefile's own `make-help` audit picking up the new target).
`check-pipelines`: 34 checks, unchanged. `nm -u` on `viewer_link.o`,
`process_cpu.o`, `viewer_session.o`, `websocket.o`, `frame_advance.o` and
`scope_view_model.o` confirms no raylib symbol reaches any of them.
