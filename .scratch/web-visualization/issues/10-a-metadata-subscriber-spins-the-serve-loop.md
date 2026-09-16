# 10 - A metadata subscriber spins the serve loop

Status: needs-triage

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

- [ ] Find why `viewer_link_poll()`'s `select()` does not wait its timeout
      when a client is subscribed to a per-iteration stream. Instrument the
      loop rate directly rather than inferring it from message counts.
- [ ] Decide what the correct publication rate for `receiver_state` is. It
      carries the applied tuning and ADR-0027's tuning generation, which change
      only on a retune; "every iteration" was chosen so a retune cannot be
      missed, and that argument is satisfied by any rate at or above the block
      rate.
- [ ] The same question for `link_health`, whose `server_cpu_percent` already
      refreshes only once a second while the message goes out every iteration.
- [ ] Whatever the fix, pin the rate in `check-viewer-link` so a stream cannot
      quietly return to free-running.
- [ ] Re-measure all five rows above afterwards and record them here.

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
