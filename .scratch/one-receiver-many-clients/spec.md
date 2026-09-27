# One receiver, many clients

Status: needs-triage -- evaluated 2026-09-27, not started.

## The idea

One process owns the receiver and exposes sockets. Everything that *looks* at
the signal is a client of it: the browser Viewer (which already is), a window,
and a script.

Raised while renaming the binaries (`.scratch/cli-subcommands/issues/04-*`).
Worth keeping because it is a real capability this program cannot offer today
and because it is where the layering work has been heading anyway -- not
because it is the next thing to build.

## What it would buy

**A receiver can only be opened once.** Today `sdrprobe-gui` and
`sdrprobe headless` cannot run against the same dongle at the same time; the
second fails at `device_open`. So there is no way to watch a band in the
window *and* have a script log it, or to have two people look at one
receiver. That is the whole of the prize, and it is a real one.

## What is already built

`sdrprobe web` **is** this shape for one client. ADR-0027 put the boundary
after the DSP, built the link, the subscribe protocol, the drop-rather-than-
queue machinery and the inbound commands. So the question is not "should
there be a daemon" -- there is one -- but whether the **window** and a
**stdout client** join the browser on the far side of it.

## The four things in the way, measured

1. **Decoding cannot move to the clients.** ADR-0027 did this arithmetic:
   derived state is ~0.5 MB/s, the same second of raw I/Q is 32 Mb/s (U8) or
   64 (S16) *per client*, "and each Viewer would then repeat the most
   expensive part of the DSP". On loopback the bandwidth is survivable for a
   local window; the repetition is not -- the GSM SCH decode is 15 ms of a
   65.5 ms block and the LTE cell search 14. Two clients decoding
   independently is over budget on this machine.

   So the daemon decodes and ships a **view model per screen**. Three exist
   (`scope`, `survey`, `fm`, plus `receiver`); six technologies remain, which
   is `web-visualization/07`'s work. **This ticket is downstream of that one**
   rather than an alternative to it.

2. **The window reads `struct app`.** `app.h`'s own header comment says the
   view split so far "is an organisation of the same coupling, not a set of
   modules". Making the window a client means every `view_*.c` reads a view
   model instead -- `.scratch/layer-boundaries/` ticket 03's endgame, not
   done, and the largest single piece of work in this list.

3. **The receiver lease would become distributed.** `receiver_borrow()`,
   `_return()` and `_commit()` are in-process today, and real things depend on
   them: the survey walks the tuner, calibration borrows it, the GSM view
   retunes on entry and restores on exit, and the lease already refuses an
   out-of-order return. Over a socket that becomes a lease protocol with
   timeouts and crash recovery -- a different problem, and one with no
   existing code here.

4. **Raw I/Q is still needed by two screens.** ADR-0005 keeps raw centred
   complex I/Q at the rendering seam precisely so the spectrum and scatter
   views can exist; ADR-0027 deliberately does *not* send it. A window client
   drawing scatter would need it, so either the link gains a raw stream (which
   ADR-0027 permits "for capture" but not for ordinary viewing) or the scatter
   view moves server-side into a view model, which is what
   `scope_view_model`'s `scatter_i`/`scatter_q` fields were already put there
   for.

## Fixed constraint: `headless` does not become a client

Decided 2026-09-27, by the operator, and it is not a detail.

`sdrprobe headless --file x.bin --decode --once` runs **in process** and
writes to **stdout**. It does not connect to anything, and a daemon must
never become a prerequisite for it.

The reasons are worth writing down so a later ticket does not quietly undo
it:

- It is the most common scripted use in this repository, and it has to work
  on a capture with no receiver present at all.
- `acquisition_set_lossless()` exists so that a scripted decode sees **every**
  block and answers the same twice. The live path's rule is the opposite --
  ADR-0002 has a slow renderer *drop* blocks rather than lag -- and a socket
  between them would have to be both.
- A one-shot that spawns a daemon and connects to it is strictly worse than
  one that does the work: more failure modes, more latency, nothing gained.

So a daemon is an **additional** mode, never a replacement. Whatever gets
built here, `headless` keeps its own path.

## If it is built

Nothing in the current CLI forecloses it, and that was checked rather than
assumed. `sdrprobe web` already means "serve, and start a client"; a daemon
would add something like `sdrprobe serve` (serve, start nothing) and an
`--attach` for a client, both additive. `sdrprobe-gui` is already the right
name for what would become the window client.

## Do not start this before

- `web-visualization/07` -- the six remaining view models. Without them the
  daemon has nothing to send a window client.
- `layer-boundaries/03`'s endgame -- views taking view models rather than
  `struct app`.
