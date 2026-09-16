# 10 - A metadata subscriber spins the serve loop

Status: resolved, 2026-09-16

## What was measured

Opened from the measurement ticket 09 asked for and did not have: what does
`--serve` cost when nobody is watching, and what does a metadata-only
subscriber cost?

Live receiver, 100 MHz, 2 MS/s, the default 2048-point transform. Server
process CPU from `/proc/<pid>/stat`, 20 s window, one run each unless noted:

| subscribed streams | server CPU |
| --- | --- |
| no client at all | 9.8% |
| `spectrum` | 10.0% |
| `receiver_state` | 98.6% |
| `link_health` | 97.7% |
| `receiver_state,link_health` | 61.3%, 63.5% (two runs) |

And the rate the streams actually arrive at, timing 100 messages with
`scripts/viewer_client.py --count 100` against one server:

| stream | 100 messages in | rate |
| --- | --- | --- |
| `spectrum` | 6.59 s | 15.2/s |
| `receiver_state` | 0.058 s | >1700/s |
| `link_health` | 0.056 s | >1800/s |

15.2/s is exactly the block rate -- 2 MS/s over `SAMPLE_BLOCK_PAIRS` is 15.26 --
so the spectrum stream is right. The other two are three orders of magnitude
faster than anything produced them. Sustained runs agree and are worse:
412 645 `receiver_state` messages in 4 s (103 k/s, 121 Mbps over loopback) and
459 963 in 5 s (92 k/s, 108 Mbps).

**So a Viewer that subscribes to the cheapest stream on the link pins a core,
and one that subscribes to the most expensive costs 10%.**

## Why this outranks ticket 09

Ticket 09 proposes to skip the Scope DSP for a Viewer that asked only for
`receiver_state` / `link_health`. The measurement above says that
configuration's cost is **not** the DSP: the whole Scope path -- conversion,
magnitudes, statistics, transform, peak hold, waterfall row -- is the
difference between 9.8% and nothing, while publishing metadata to one
subscriber is 90%. Ticket 09 would remove about a tenth of what a metadata-only
Viewer actually costs and leave the rest.

It does not make 09 wrong. It makes it second, and it changes what 09's own
acceptance criteria should measure against.

## What is established, and what is not

**Established**: the rate, the CPU, and that it depends on there being a
*subscriber* rather than a client. A connected client subscribed only to
`spectrum` costs the same as no client at all, so this is not the cost of
having a socket open.

**Not established: the cause.** The obvious candidate is ruled out. The publish
is per-iteration by design and documented as such (`viewer_session.c:143-148`,
"Every iteration, not gated on `spectrum_updated`"), which is correct for a
replaceable stream -- but it should then be paced by
`viewer_link_poll(&link, VIEWER_SESSION_POLL_MS)` at 20 ms, or 50 iterations a
second. It is not. `viewer_link_poll()` registers a client for writing only
when a slot has unsent bytes, and a run with `--debug-log` recorded **zero**
`stalled` and zero `stall cleared` lines, so nothing was left pending and
`select()` had an empty write set with a 20 ms timeout it did not wait out.

Establishing why `select()` returns early is the first task here, and nothing
should be changed before it is known. The 61% reading for *both* metadata
streams against 98% for either one alone is unexplained by any theory offered
so far and is probably the same question from another side.

## Tasks

- [x] Find why `viewer_link_poll()`'s `select()` does not wait its timeout
      when a client is subscribed to a per-iteration stream.
- [x] Decide what the correct publication rate for `receiver_state` is.
- [x] The same question for `link_health`.
- [x] Pin the rate in a check so a stream cannot quietly return to
      free-running.
- [x] Re-measure all five rows above afterwards and record them here.

## The cause

**`select()` was never at fault, and neither was the poll.** Instrumented
directly -- counting calls, their mean duration, and which sets came back
ready -- the two cases are unambiguous:

```
no client:        45 calls/s, mean 20.383 ms, 0 under 1ms, 45 timed out,
                  ready: listen 0 read 0 write 0
receiver_state:   101749 calls/s, mean 0.001 ms, 101749 under 1ms, 0 timed
                  out, ready: listen 0 read 0 write 101749
```

With no client the poll waits its whole 20 ms timeout, every time. With a
subscriber the write set is ready on **every one of a hundred thousand calls**
and not one of them times out.

The reason is one line in `viewer_link_publish_receiver_state()`: it **queues
and does not send**, setting `slot->length` with `slot->sent` at 0.
`viewer_link_poll()` then correctly registers that client for writing, and a
loopback socket is always writable, so `select()` returns at once. The 20 ms
timeout is the loop's only pacing, and publishing unconditionally on every
iteration guaranteed there was always something pending -- so the timeout
could never be reached. Publish, return immediately, flush, publish again.

It is a feedback loop rather than a leak, which is why nothing looked wrong at
any single point: every part behaves exactly as its own comment says. The
fault is the composition, and it is invisible from the source of any one of
them.

`spectrum` never showed it because it is published only on `spectrum_updated`
-- 15.2 a second -- so between blocks nothing is pending and the poll idles.

## The fix

`receiver_state` carries centre, rate, correction, tuning generation and full
scale, and **every one of those changes only on a retune**. So it is published
when the tuning generation differs from the one last sent -- which makes a
retune immediate -- and otherwise on a quarter-second heartbeat that bounds
how long a newly connected Viewer waits to be told where the receiver is
pointed. `link_health` goes out with its own CPU sample, once a second,
because `server_cpu_percent` cannot be fresher than that.

`viewer_update_due()` in `viewer_session.h` is the decision, and it is a
header function rather than an `if` in the loop because the loop takes
`struct app` and nothing windowless can reach it (ADR-0012). It is checked by
**`check-viewer-session`** rather than `check-viewer-link` as this ticket
first assumed: the pacing is the session's, not the link's, and a check in the
link's suite would have been testing a file it does not own.

Two details the check pins because both are easy to get wrong. "Never
published" is a **negative** sentinel, not 0.0 -- a loop timing from its own
start reaches a real 0.0, and a check using 0.0 for both would pass either
way. And a change beats the interval, which is what stops a Viewer drawing a
quarter second of measurements under the previous frequency.

## After

Same receiver, same 20 s windows, same measurement:

| subscribed streams | before | after |
| --- | --- | --- |
| no client at all | 9.8% | 9.6% |
| `spectrum` | 10.0% | 9.5% |
| `receiver_state` | 98.6% | **9.7%** |
| `link_health` | 97.7% | **9.5%** |
| `receiver_state,link_health` | 61.3%, 63.5% | **9.4%** |

And the streams still arrive, at the rates they were designed for -- 8 s with
all three subscribed: `spectrum` 122 messages (15.25/s, the block rate),
`receiver_state` 31 (3.9/s), `link_health` 8 (1.0/s). Loopback throughput went
from 121 Mbps to 2.0, and all of the 2.0 is now spectrum.

A retune still reaches a Viewer at once rather than waiting out the heartbeat:
with a scripted retune five seconds in, a subscribed client reads ten
`receiver_state` messages at 100 MHz and generation 0, then one at 104 MHz and
generation 1.

`make check`: 21624 checks in 77 suites, no failures.

## Not in scope

- Ticket 09's subscription-driven computation. It should be re-argued against
  these numbers once this is fixed, not merged into it.
- Changing the replaceable-slot rule. Nothing here suggests a Viewer is
  receiving stale data; it is receiving far too much fresh data.

## Comments

**2026-09-16** -- This also corrects a claim I put in ticket 09 and in
`bab6319`'s commit message: *"Checked and not a risk: demand-off does not
busy-spin -- `viewer_session.c:165` paces every iteration on
`viewer_link_poll()`."* That was read off the source and never measured, and it
is false in exactly the configuration ticket 09 is about. The right reading of
`viewer_link_poll()` is that it *should* pace the loop; what it does is the
subject of this ticket.
