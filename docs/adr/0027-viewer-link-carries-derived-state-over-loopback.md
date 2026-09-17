# The Viewer link carries derived state over loopback

## Status

accepted

## Context and decision

This program draws to one raylib window on the machine holding the receiver.
Watching it from another room, or running it on a machine with no display at
all, has no route: every headless path prints a report and exits, and the
window is the only live presentation there is.

Three network boundaries were available. After acquisition, streaming raw I/Q
and processing it in the browser. After DSP, streaming the measurements the
native process has already made. Or no network boundary at all, compiling the
whole application to WebAssembly and running it in a canvas.

The boundary is therefore **after DSP**. The native process keeps acquisition,
receiver control, DSP, decode sessions, recording, survey state and
persistence. It serves a **Viewer link** on 127.0.0.1 carrying **State
updates** out and **Viewer commands** in; a **Viewer** draws what it is sent
and owns nothing else. The wire carries domain measurements, never raw I/Q for
ordinary viewing and never a C struct's memory layout.

The arithmetic decides it rather than taste. A sample block is 131072 pairs,
65.5 ms at the house 2 MS/s rate, so 15.26 blocks arrive per second. Derived
state -- spectra, a quantized waterfall row, decimated scatter, receiver
metadata -- is about 0.5 MB/s. The same second of raw U8 is 32 Mb/s and raw
S16 is 64, **per Viewer**, and each Viewer would then repeat the most
expensive processing in the program: the GSM synchronization decode is about
15 ms of a block and the LTE cell search about 14, natively, at `-O3`.

Loopback is part of the decision and not a deployment detail. The bind address
**is** the authorization boundary: reaching 127.0.0.1 already requires an
account on the machine, so a Viewer command carries no credential, no token
and no transport security, and none of those are missing. Binding beyond
loopback is a different decision that owes authentication before it allows
control, and it is not taken here.

State updates are replaceable and Viewer commands are not. A newer State
update supersedes an unsent older one rather than queueing behind it, which is
ADR-0002's freshness rule carried onto the wire: a slow Viewer stays current
instead of replaying a backlog. Commands and their outcomes are reliable and
ordered, because a dropped retune is not a stale picture but a receiver
pointed somewhere nobody asked for.

## Considered options

- **Stream raw I/Q and process it in the browser** is unusually feasible here,
  because the rule that keeps the DSP checkable -- it links `-lm` alone and
  never raylib -- is also the precondition for compiling it to WebAssembly.
  It is declined for live viewing on the arithmetic above: it inverts the
  bandwidth, multiplies the processing by the number of Viewers, and is worst
  on exactly the small headless machine that motivated remote viewing. It
  remains available for capture playback, where none of those costs apply.
- **Compile the whole application to WebAssembly** is a port rather than a
  feature. `run_gui()` is a `while (!WindowShouldClose())` loop carrying the
  input precedence chain and would have to yield to the browser's event loop
  or pay Asyncify; `acquisition.c`'s pthreads need SharedArrayBuffer and
  COOP/COEP headers, hence threaded and unthreaded builds; the receiver is not
  a recompile of `backend_rtlsdr.c` but a new WebUSB backend, absent from
  Firefox and Safari; and config, surveys and captures each need an IndexedDB
  or OPFS adapter.
- **Expose the Viewer link beyond loopback** removes the need for a tunnel and
  is one line of code, but it makes authentication, transport security and a
  threat model prerequisites of the control path rather than later work.
- **Serve derived state but make it view-only** avoids the inbound half of the
  protocol entirely. It was declined because a Viewer that can see a signal
  and not retune to it sends the operator back to the window, which is the
  thing remote viewing exists to avoid.

## Consequences

- This is the first socket in the program. `grep` for `netinet`, `sys/socket`,
  `arpa/inet`, `poll.h` or `select` over `src/` returned nothing before it,
  and no port number appeared anywhere. The HTTP and WebSocket handling is
  hand-written for the reason `vendor/` has stayed at one entry, and is pure
  byte manipulation with published test vectors, so its check links `-lm`
  alone and satisfies ADR-0012.
- The wire format becomes an interface in ADR-0016's sense, and is versioned
  against that rather than against the screens.
- The per-block work must be reachable without a window, so the advance
  between `consume_latest()` and `BeginDrawing()` becomes a step both the
  window and a headless run drive. Lifting `render_waterfall()` and
  `update_scatter()`'s texture upload into the draw phase is ADR-0012's own
  rule -- a function that draws may not also decide -- arriving by a different
  route.
- A Viewer's subscription takes over what the active tab decides today. With
  no screen there is nothing else to gate expensive per-block work with, and
  the block budget does not allow computing every technology for every Viewer.
- Raw I/Q over the Viewer link is a separate capability, permitted for capture
  playback or an experimental WebAssembly worker, and never the path ordinary
  viewing takes.
- **Capture analysis in WebAssembly remains open.** It needs no receiver, so
  no WebUSB; no live stream, so none of the bandwidth above; and no server, so
  none of this ADR's security reasoning. It shares no code with the Viewer
  link and is a separate decision whenever somebody wants it.

## Amendment, 2026-09-16 -- the seam is a view model, and the window is not a peer

Taken the same day, before any of this was built.

**The window and a Viewer are not run at once.** They are alternative
frontends rather than two live presentations of one receiver, and `--serve`
therefore drives the per-block advance directly without initialising raylib.
This removes a class of problem rather than adding one: there is no second
presentation to keep in step with the first, and no question about which of
them owns the receiver.

**What they share is a view model**: plain data saying *what* a screen shows,
produced once by the advance step. A Viewer's State update is that view model
serialized; the raylib views read the same object. The window remains the
primary and fully-featured presentation, and migrating its views onto the view
model happens one at a time, each independently verifiable, each declinable.
Retiring it is not decided here and would be a further amendment, which would
owe an accounting of the apparatus that is raylib-shaped: ADR-0012's
windowless checks, the `*_layout.h` headers, `check-layout`, `panel_rows.h`
and `make screens`.

**A display list was considered and declined.** Views emitting rectangles,
text and polylines for either backend to rasterize is the obvious shape and it
is wrong here, because geometry in this program is computed from raylib's font
metrics: `MeasureText` appears at 57 sites across 13 files, *including*
`scope_layout.h` and `sdrgui_geometry.h`. A browser's metrics differ, so a
positioned display list either lays out wrongly there or forces a canvas with
the identical font -- a remote framebuffer, discarding the responsive layout,
real text input, selectable text and accessibility that are the reasons to
have a web frontend. A view model says what to show and lets each frontend lay
it out, so the two are allowed to look different.

**Half of this already existed and was half-adopted.** `sdrgui.h`'s components
are already on the correct side of the seam -- ADR-0007 has them taking plain
data and geometry and never seeing `struct app`. The views bypass them 174
times with direct raylib calls, 104 of them bare `DrawText`. The amendment
does not fix that; it moves the *data* behind a view model and leaves the
drawing where it is.

**The input half is explicitly unsolved.** 161 raylib input call sites in the
views and overlays, with hit-testing run inline against rectangles that exist
only during drawing -- immediate mode entangles input with layout by
construction. A Viewer command is not a click and does not need this solved. A
browser reproducing the window's *interactions* would, and nothing here
claims it can.

## Amendment, 2026-09-17 -- binding beyond loopback, behind a token

The original decision read, in `viewer_link.c`'s own comment: "Loopback
only -- this is not a configuration option here." That line is no longer
true, and this amendment is what makes the change deliberate rather than
a quiet erosion of it.

**Why now.** An operator on the same LAN as the receiver -- a second
laptop, a phone -- has no route to the Viewer today short of an SSH
tunnel, which works but is a separate credential to manage for what is
often a single trusted home network. The "Considered options" section
above already named the alternative and declined it for a specific,
narrow reason: *"it makes authentication, transport security and a
threat model prerequisites of the control path rather than later
work."* This amendment is that work, done rather than deferred, and
scoped no wider than the LAN case that asked for it.

**What changed.** `viewer_link_open()` takes a bind address
(`INADDR_LOOPBACK`, `INADDR_ANY`, or one interface's own) and an optional
required token. `options.c` refuses the combination that matters --
binding beyond loopback with no token -- at parse time, before a socket
is ever opened: `--serve-bind any|ADDRESS` requires `--serve-token
SECRET` in the same invocation. Every HTTP request the link receives,
upgrade or plain page, must then carry `?token=SECRET` in its path
exactly, checked before either is served (`token_authorized()`,
`viewer_link.c`). Loopback with no token is completely unchanged: the
default is still `SERVE_BIND_LOOPBACK`, still needs nothing else, and
every existing caller (the window's own headless serve, every test, every
capture-driven `--serve` invocation this repository has) is unaffected
byte-for-byte.

**What a shared secret in a URL is not.** It is not per-user identity --
every device with the token is indistinguishable from every other. It is
not transport security -- the token crosses the LAN in plaintext inside
an unencrypted WebSocket handshake, visible to anything else on that
network segment that can see the traffic at all. It is not durable
against the token leaking -- browser history, a saved bookmark, a proxy's
access log, or a screenshot showing the URL bar all compromise it until
it is changed, and there is no rotation and no expiry. It does not
protect a network with an untrusted device already on it, which is
exactly the case loopback's original reasoning ("reaching 127.0.0.1
already requires an account on this machine") never had to consider. What
it does provide: a device without the token gets nothing at all -- not a
page, not a state stream, not a command channel -- which is the minimum
a trusted-home-LAN operator asked for and the maximum this amendment
claims.

**What was measured before this shipped**, live against a real
`sdrprobe --serve` process bound to a non-loopback address (`127.0.0.2`,
routed over loopback but exercising the real non-default `bind()` path
rather than a synthetic one): a request with no token and a request with
the wrong token are both refused with `401 Unauthorized` before an
upgrade is attempted or the page is sent; the correct token succeeds for
both the plain page and the WebSocket upgrade; and the default (no
`--serve-bind`) path is unchanged, confirmed by diffing its startup
message and behaviour against what it printed before this amendment.

**A pre-existing, unrelated bug surfaced during that testing and is fixed
alongside this amendment**: `viewer_session_run()`'s loop never read
`app->options.duration_seconds` at all, checking only
`stop_requested()` (SIGINT/SIGTERM) -- unlike `sdrprobe.c`'s other
headless loop (decode/playback), which has always honoured its own
duration. Every `--serve ... --duration N` invocation, with or without
this amendment's flags, silently ran until killed rather than stopping
at N seconds. `viewer_duration_elapsed()` (`viewer_session.h`, mirroring
`viewer_update_due()`'s own shape as a pulled-out decision, ADR-0012) is
the fix, unit-tested in isolation from the loop it now gates.

**Not addressed, and named rather than left implicit**: TLS (a
self-signed certificate a browser would warn about on every connection,
for a tool meant to be opened without ceremony, was judged worse than
the plaintext token for this use case); token rotation or expiry; rate
limiting on failed attempts (a wrong token costs one refused TCP
connection, not a lockout); and multiple tokens or any notion of which
device is which. Each is a real gap for a network this amendment does
not claim to cover -- anything other than a small, trusted home LAN
should keep using the SSH tunnel this amendment does not replace.
