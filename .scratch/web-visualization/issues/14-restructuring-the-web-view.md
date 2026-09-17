# 14 - Restructuring the web view, before it becomes what ADR-0007 already fixed once

Status: needs-info -- Phases 1, 2 and 3 done (2026-09-17); Phase 4
(ticket 07's remaining views) still open. See Comments.

## Why now

ADR-0007 opens by describing the native UI as it was:

> `sdrprobe`'s UI was one ~3600-line file mixing three concerns: generic
> widgets [...], bespoke SDR visualizations [...], and the application logic.

**That is the web view today, at 341 lines instead of 3600.** One file,
`src/viewer_page.h`, holding generic formatting (`formatBytes`,
`formatBitsPerSecond`, `dbfsToY`, `colorFor`, `plot`), bespoke SDR drawing
(`drawSpectrum`, `drawWaterfall`, `drawSurveyChart`), view composition
(`renderSurveyState`, `renderHealth`, `showTab`) and the application itself
(`connect`, `ws.onmessage`, `handleState`, the generation rule, the
subscription line) -- with no seam between any of them.

The difference is that the native side got to 3600 lines before anybody
minded, and this one has a **written schedule to grow**: ticket 07's remaining
list is FM, GSM, ADS-B, TETRA, LTE, SRD and two overlays, each wanting a
panel, a chart and a table. Six more views into this file reproduces the file
ADR-0007 was written to break up.

So this is not a new architectural decision. It is applying one already made,
to the frontend that did not exist when it was made.

## Architecture

Three layers, mirroring ADR-0007's split, plus the wire format which has no
native counterpart.

```
web/
  viewer.html          the shell document: <head>, the tab bar, the panel
                       mount points, the health panel. No view markup.
  lib/
    chart.js           axes, gridlines, dBFS-to-y, plot geometry, colour
    table.js           a header row and n data rows into a <tbody>
    format.js          bytes, bits/sec, hertz, decibels
  wire.js              decode one message: the binary headers and the JSON
                       payloads, in one place
  views/
    scope.js           spectrum + waterfall
    survey.js          sweep chart + candidate table
    (fm, gsm, adsb, tetra, lte, srd to follow -- ticket 07)
  viewer.js            the shell: socket, reconnect, generation rule,
                       subscription, tab routing, health. Composes; draws
                       nothing.
```

**The rule each layer obeys is ADR-0007's own, restated for JavaScript:**

- `lib/` takes **plain data and geometry** -- a canvas context, an array, a
  rect, a style -- and never sees a message, a socket or a view. This is
  `sdrgui.h`'s contract (*"Each takes a plain param struct (buffers + geometry
  + style), never `struct app`"*) with `struct app` replaced by "the
  application".
- `views/` is one module per view, the way the native side is one file per
  screen (`view_scope.c`, `view_gsm.c`, ... with `src/view.h` declaring what
  they share). Each module exports three things and nothing else:
  - `markup` -- the panel's HTML,
  - `streams` -- the stream names this view needs,
  - `render(state)` -- draw from a view model, having asked for nothing.
- `viewer.js` owns everything that is not drawing: the connection, the
  reconnect, ADR-0027's declining of stale generations, which tab is showing,
  and what to subscribe to.

**`render(state)` takes the view model's own JSON**, not fields assembled ad
hoc. That is the whole point of tickets 03 and 07 -- `spec.md`'s own words are
that the raylib window and a browser Viewer are *"alternative readers of one
object rather than two presentations kept in step"*. A web view module that
reshapes the data on arrival quietly re-introduces the second presentation.

### Subscription follows the view

Today the page subscribes to all six streams at connect and keeps them for the
session, whichever tab is showing. With `streams` declared per view, the shell
sends a fresh subscribe line on every tab switch -- which the protocol already
supports, since *"a fresh line replaces the whole subscription set"*.

Worth naming precisely so it is not oversold: this is the **client half** of
ticket 09. It stops the server *sending* what nobody is looking at; ticket 09
is about not *computing* it. But ticket 09's payoff cannot be measured at all
while every client subscribes to everything, so this unblocks measuring it.

### One response or many: decide before writing code

Per-view modules can reach the browser two ways, and the choice is not
cosmetic:

- **Concatenated at build time** into one page, exactly as today. `serve_page()`
  is unchanged -- one buffer, one `Content-Length`, one response, no routing.
- **Real ES modules**, `<script type="module">` with each file fetched. This
  requires `serve_page()` to become a **static file server with a path
  router**, which brings path traversal into a program that currently cannot
  have it: today there is precisely one response and no filename ever reaches
  the filesystem.

**Prefer concatenation.** The editing and checking benefits come from the
*source* being separate files (ticket 13), and they are fully realised without
serving them separately. Over loopback the round trips are cheap, but a path
router on a socket is a security surface bought for nothing.

## What gets implemented

1. `web/` as the source of truth, generated into one compiled-in page
   (ticket 13 is this phase, in detail).
2. The three-layer split above, with the **two existing views behaving
   identically**.
3. A view registry in `viewer.js`: `views = [scope, survey]`, panels mounted
   from `markup`, `render()` dispatched on the active tab, `streams` driving
   the subscribe line.
4. `wire.js` as the single decoder, with the message-type constants named
   once. Today those constants are literals in `ws.onmessage` and the
   `viewer_page.h` comment admits the duplication: *"the browser's copy of the
   wire format lives here beside the page"*. One reader today, five after
   ticket 07.
5. Ticket 07's remaining views then land as `views/*.js`, each a module rather
   than another few hundred lines in the one file.

## Plan

**Phase 1 -- assets as files.** Ticket 13, unchanged. Prerequisite for
everything else; the bytes served must be identical when it lands.

**Phase 2 -- the seam, with no behaviour change.** Extract `lib/`, `wire.js`
and `viewer.js`; leave Scope and Survey as they are, merely relocated. This is
a refactor that is supposed to change nothing, and `CLAUDE.md` is explicit
about how that goes wrong here: the survey machine came out of its view *"with
55 suites green, both capture surveys byte-identical and the screen
byte-identical, while the settle that throws away stale blocks was disabled"*.
So Phase 2 is measured **behaviourally, live** -- a real connection, a real
generation bump, a real reconnect -- not by the page still loading.

**Phase 3 -- the registry.** Views become modules with `markup`/`streams`/
`render`. Subscription follows the active view. Adding a view stops being an
edit to the shell.

**Phase 4 -- the remaining views.** Ticket 07's list, one module and one
commit each, as that ticket already requires.

Phases 1 and 2 are worth doing even if 3 and 4 are never scheduled. Phase 3
without Phase 2 is a registry over a monolith.

## Tasks

- [x] Ticket 13 lands: `web/` source, generator, `build/viewer_page.h`,
      byte-identical output.
- [x] `web/lib/format.js` -- `formatBytes`, `formatBitsPerSecond`, and the
      hertz/decibel spellings currently inline.
- [x] `web/lib/chart.js` -- `dbfsToY`, `plot`, `colorFor`, axis and grid
      drawing, taking a context and a rect. (No gridlines are drawn yet by
      either view, so there is no axis/grid function to extract; the file
      comment says so rather than inventing one nobody calls.)
- [x] `web/lib/table.js` -- the candidate table's row rendering, which the
      decode views will each want.
- [x] `web/wire.js` -- message type constants and one decode entry point;
      binary headers and JSON payloads in one module.
- [x] `web/viewer.js` -- socket, reconnect, ADR-0027 generation rule,
      subscription, tab routing, health panel.
- [x] `web/views/scope.js`, `web/views/survey.js` -- `markup`, `streams`,
      `render(msg)`, nothing else exported (Phase 3): each wrapped in its
      own IIFE so its DOM refs, private state and draw functions stay out
      of the shared global scope every concatenated file runs in, and the
      view object itself is the only name it contributes.
- [x] The registry in `viewer.js`; `showTab()` (renamed `selectView()`)
      mounts and dispatches rather than toggling two known panels --
      `VIEWS.forEach()` over the registry, not two named ids.
- [x] Subscribe line rebuilt from the active view's `streams` on every switch
      (`subscribeToActiveView()`), plus the shell's own `receiver_state`/
      `link_health`, which are not any view's concern.
- [x] The generator concatenates in dependency order and the order is stated
      in one place, not implied by filenames (`JS_ORDER` in
      `scripts/embed_web.py`; the Makefile asks the script for that same
      list via `--list` rather than keeping a second copy).
- [x] `viewer_page.h`'s rationale comment survives the move (ticket 13 owns
      where it goes) -- it opens `build/viewer_page.h` itself, generated by
      `scripts/embed_web.py`.

## Evaluation criteria

**Behavioural, and all of them checkable without a person:**

- [x] With one view showing, the server sends **only** that view's streams --
      measured on the wire, by message type, not read off the subscribe line.
      Confirmed two ways: the Node harness's wrapped `WebSocket.send()`
      shows the exact subscribe line the shell sends on each switch, and,
      independently, a raw script-level client (`ViewerClient.send()`,
      bypassing `viewer_client.py`'s own pre-ticket-07 stream allowlist)
      subscribed first to `spectrum,waterfall,receiver_state,link_health`
      and received only those four types over 2.5 s (276/275/10/2
      messages), then subscribed to
      `survey_spectrum,survey_state,receiver_state,link_health` over the
      same server and received **zero** spectrum or waterfall messages.
- [x] Switching tabs changes the subscription within one message, and
      switching back restores it. Verified live: clicking Survey sends a
      fresh `subscribe` line naming `survey_spectrum`/`survey_state` and
      dropping `spectrum`/`waterfall`; clicking back to Scope sends
      another restoring the original set.
- [x] A message stamped with a stale `tuning_generation` is still declined
      after the restructure. This is *"the one rule this page exists to
      prove"* (ADR-0027) and the single most likely casualty of moving the
      decode out of `ws.onmessage`. Verified live after Phase 2: a Node
      harness loads the page's *actual served script* into a fake DOM,
      drives a real WebSocket against a running `sdrprobe --serve`, forces
      a real generation bump via a synthetic `receiver_state`, then feeds
      the real `onmessage` handler a binary spectrum frame stamped with
      the superseded generation -- observed declined (no new canvas draw
      call) and counted (the hud's own "declined" figure, read back
      through the DOM text the page itself writes, rose by exactly one).
- [x] A dropped connection still reconnects and still shows the newest state
      rather than a backlog (ADR-0002's freshness rule, as the page
      demonstrates it). Same harness: the real socket is closed, the hud
      reports disconnection, a new WebSocket appears within the 1 s
      backoff, and a fresh `receiver_state` is shown.
- [ ] `make bench-serve` shows no regression: a restructure must not
      reintroduce ticket 10's or ticket 07's spin, and a subscription that
      follows the view should show an *improvement* with a single-view client.
      **Attempted against the live receiver, and left unresolved rather
      than reported on one flattering run.** Round 1 (no client, then
      everything, then Scope-only, 15 s each): 30.0%, 42.9%, 21.3% of a
      core -- a clean story, everything costing double Scope-only.
      **Round 2, same three cases in a different order**, immediately
      after: 42.8%, 43.2%, 40.1% -- all three within 3 points of each
      other, no separation at all. The whole-machine load moved by more
      between the two rounds than any subscription set moved a single
      round, which is `does-it-help`'s own warning about drawing noise
      once. **Not claimed as an improvement, and not claimed as a
      regression either** -- this needs a quieter machine or several more
      rounds averaged, and this ticket does not have that measurement to
      report today. What Phase 3 *does* establish, on the wire rather
      than from `/proc`: a Scope-only subscribe line reaches the server
      and only spectrum/waterfall/receiver_state/link_health come back,
      never survey_spectrum/survey_state, and vice versa (evaluation
      criterion above, `x`ed). Whether that translates to a measurable
      server-side saving is a separate, still-open question.
- [ ] Ticket 11's structural check passes against the restructured page, and
      each `views/*.js` is loadable by it in isolation. **Ticket 11 is not
      built**; `web/views/scope.js` and `web/views/survey.js` exist as
      plain files today, which is as far as this ticket can move that
      criterion before ticket 11 itself is picked up.
- [ ] Adding a seventh view touches `views/` and the registry line, and no
      other file. **Phase 3** (there is no registry line yet).

**Structural:**

- [x] No file in `lib/` references a socket, a message, a stream name or a
      view. True of `web/lib/format.js`, `chart.js` and `table.js` as
      written.
- [x] No file in `views/` opens a socket or names a message type. True of
      `web/views/scope.js` and `survey.js` as written -- `viewer.js` is the
      only file that imports `wire.js`'s constants.
- [x] No framework, no CDN, no npm, no bundler (ticket 01's constraint,
      unchanged).
- [x] The native window is untouched: no `view_*.c`, `sdrgui*` or raylib file
      was edited across all three phases (`git diff --stat` touches only
      `web/`, `scripts/embed_web.py` and `scripts/viewer_client.py`
      (Phase 3's own bench-serve fix), `Makefile`, `AGENTS.md` and this
      ticket's own files). `make check` passes at 77 suites throughout --
      21774 checks after Phase 2, 21781 after Phase 3, a difference that
      is noise (nothing under `check-*` reads `web/`, confirmed by the
      `MISSING:`/`NOT GATED` audits staying clean at every phase) rather
      than a suite this ticket touched growing or shrinking. `make
      screens` was not re-run -- nothing it draws could have moved.

## Take into account

- **This is web-only.** ADR-0027 keeps the raylib window primary and nothing
  here changes a native file. A commit in this ticket that edits `view_*.c` is
  doing something else.
- **The view model is the seam, and it is on the C side.** If a web view finds
  itself needing a field the view model does not carry, the answer is to add
  it to the view model -- where `check-*` can reach it -- and not to compute it
  in JavaScript. That is ADR-0012's rule about deciding, applied across the
  socket.
- **Phase 2 has no visible output**, which is exactly the situation
  `does-it-help` and the survey-machine story warn about. Decide its
  measurement before starting it, not after.
- **`CHECK_UNITS` and `APP_HDR`** after any new rule or header, both one line
  in `CLAUDE.md`.

## Relationship to the other tickets

- **13** is Phase 1 of this ticket, in detail. Do not do this one first.
- **11** (looking at the page) becomes far easier after Phase 2 and is listed
  in the evaluation criteria above.
- **12** (the three missing checks) is independent -- C side, no overlap.
- **07** (the remaining views) is Phase 4, and is the reason this is worth
  doing at all.
- **09** (subscriptions drive computation) gains a measurable client here, and
  is not done by it.

## Comments

**Phase 1 (ticket 13) and Phase 2 done, 2026-09-17. Phase 3 (the registry)
and Phase 4 (ticket 07's remaining views) are still open** -- this ticket's
own order, "do not schedule 3 without 2, do not schedule 4 without a
registry," so this is a deliberate stopping point rather than an
abandoned one.

**Phase 2's seven files, in the order they concatenate**
(`scripts/embed_web.py`'s `JS_ORDER`): `web/lib/format.js` (byte and
bits-per-second formatting), `web/lib/chart.js` (`dbfsToY`, `plot`,
`colorFor` -- `plot` is new, generalised from two copies of the same loop
that used to live separately inside `drawSpectrum` and `drawSurveyChart`),
`web/lib/table.js` (`renderRows`, generalised from the Survey table's own
inline row-building the same way), `web/wire.js` (the message-type
constants and `decodeMessage()`, replacing the bare `1`/`2`/`3` literals
`ws.onmessage` used to dispatch on), `web/views/scope.js` (spectrum,
waterfall), `web/views/survey.js` (chart, table, status line) and
`web/viewer.js`, cut down to what ADR-0007's split calls the application:
the socket, reconnect, the generation rule, tab routing and the Health
panel. None of `lib/`'s three files or `views/`'s two reference a socket,
a message, a stream name or a view -- checked directly, not merely
intended -- and `viewer.js` is the only file that reads `wire.js`'s
constants.

**The outer `(function(){ ... })();` is gone, on purpose.** The old page
wrapped everything in one IIFE so its names stayed off `window`; seven
concatenated files sharing one `<script>` tag have no module boundary to
close over in its place; each top-level `function`/`const` in one file is
now a plain global, reachable from every file after it (and, for function
declarations, before it too, since those hoist). Checked for collisions
by hand -- there are none, each file's top-level names are unique across
all seven -- and this is the shape "concatenated at build time" (this
ticket's own decision, over ES modules) always implied; it was only
staged as a single file before now.

**Measured live, not read off the diff.** This ticket names its own
risk: *"Phase 2 has no visible output... does-it-help and the
survey-machine story warn about"* measuring a refactor by the page still
loading. A Node harness (kept in a scratch directory, not committed --
this is a one-off measurement, not ticket 11's DOM-shim check, which is a
separate ticket) loads the *actual bytes `sdrprobe --serve` returns* --
extracted from a live response, not re-assembled from the source files by
hand -- into a fake DOM (canvases that record every draw call, divs that
record `textContent`/`innerHTML`/`hidden`/`classList`) and drives it over
a **real** `WebSocket` against a **real**, running `sdrprobe --serve`.
Every assertion reads a DOM effect the harness's own fake elements
recorded, or text the page itself wrote into `hud.innerHTML` -- never a
script-internal `let`/`const`, which (exactly as in a real browser's
inline `<script>`) is not a property of anything outside the script's own
top-level scope, a fact the first version of this harness ran into
directly (`sandbox.sent`, `sandbox.ws` and friends were all
`undefined`). Thirteen checks, all passing: the page connects and shows
`receiver_state`; both `drawSpectrum` and `drawWaterfall` fire (a real
`fillRect`/`drawImage` recorded on the fake canvases); a Survey tab click
flips the panel before any server round trip; a synthetic
`receiver_state` bump followed by a binary spectrum frame stamped with
the *superseded* generation is declined -- no new draw call, and the
hud's own "declined" figure rises by exactly one, both read back through
the DOM the page itself wrote, never through a peeked variable; and
closing the real socket produces `onclose`, the hud's disconnection text,
and a fresh reconnect with a new `WebSocket` and a resumed
`receiver_state` within the 1 s backoff (ADR-0002's freshness rule).

**One test-harness trap worth naming**: the first attempt at the
stale-generation check computed "one behind whatever generation the hud
already shows," which for this capture (no retune, so `tuning_generation`
never leaves its initial value) meant "one behind 0," clamped back to 0 by
the harness's own `Math.max(0, ...)` guard against an unsigned field going
negative -- so the frame was not actually stale and the harness's synthetic
buffer was also too short for the code path that then ran, producing a
`RangeError` that looked like a bug in the page. It was a bug in the
harness's test data: a real stale frame requires a real generation *bump*
first, which is what the fixed version sends (a synthetic `receiver_state`
naming a generation five ahead) before manufacturing the superseded frame
behind it -- a shape closer to the real scenario ADR-0027 defends against
(a frame queued before a retune, delivered after it) than "current minus
one" ever was.

**`make check` (21774 checks, 77 suites) and `make check-pipelines` both
pass, run fresh after Phase 2** in addition to after Phase 1 -- neither
suite reaches the browser side, so this is confirming Phase 2 broke
nothing on the C side, not a claim about the JavaScript, which is what the
Node harness above is for.

**Not attempted in Phase 2, and worth saying so rather than leaving
implicit:** the `markup`/`streams`/`render(state)` export shape (Phase 3)
and the view registry that would dispatch on it. `views/scope.js` and
`views/survey.js` today are still called by name from `viewer.js`'s
`ws.onmessage`, the same shape as before this ticket, merely relocated --
which is exactly what Phase 2's own instruction asked for ("leave Scope
and Survey as they are, merely relocated") and exactly why the
subscription-follows-view and single-view-bench-serve criteria above are
still unchecked: there is no per-view subscription to measure until
Phase 3 builds one.

### Phase 3, 2026-09-17

**The registry.** `web/views/scope.js` and `web/views/survey.js` now each
export exactly `{id, label, tab, streams, markup, render}` -- wrapped in
its own IIFE (`const ScopeView = (function () { ... return {...}; })();`)
so `ScopeView`/`SurveyView` are the only names either file contributes to
the shared global scope every concatenated file runs in. That wrapping
was not optional: the first version of both files declared `let els =
null;` at top level for their lazily-resolved DOM refs, which is a
straight name collision the moment two view files exist side by side in
one script -- caught before it ever reached a browser, by rebuilding and
re-reading the generated header for two `let els` declarations. Canvas
and element lookups had to move from module-load time into a memoised
`elements()` helper for the same underlying reason: `markup` is not in
the document yet when a view file's top-level code runs, since
`viewer.js` -- concatenated *after* every view -- is the one that inserts
it into `#panels`, at `mountViews()` time.

`viewer.js` is cut down to the registry (`const VIEWS = [ScopeView,
SurveyView];`), `mountViews()` (builds the tab bar and every panel from
`markup`/`label`, once), `selectView()` (renamed from `showTab()`: shows
the chosen view's panel and hides every other, sends `view <id>` when a
click drove it, and rebuilds the subscription when the view actually
changed), and `subscribeToActiveView()` (`receiver_state`/`link_health`
always, plus whichever view is showing). `viewer.html`'s `#tabs` and
`#panel-scope`/`#panel-survey` collapse to two empty mount points,
`#tabs` and `#panels` -- the tab bar and both panels are entirely
generated now, so a seventh view is a `VIEWS` entry and a file, nothing
in this HTML.

**Verified against a real server, on the wire, not read off the
subscribe line.** A raw script-level client (`ViewerClient.send()`,
bypassing `viewer_client.py`'s own CLI validation) subscribed to
`spectrum,waterfall,receiver_state,link_health` and received exactly
those four types over 2.5 s -- 276/275/10/2 messages, zero
survey_spectrum or survey_state -- then, against the same server,
subscribed to `survey_spectrum,survey_state,receiver_state,link_health`
and received zero spectrum or waterfall messages. The Node harness (same
one Phase 2 built) adds the client-side half: clicking the Survey tab
sends a fresh `subscribe` line naming `survey_spectrum`/`survey_state`
and dropping `spectrum`/`waterfall` within the same message, a `view
survey` command reaches the server, and clicking back to Scope restores
the original line -- 19 checks, all passing, the ADR-0027/ADR-0002
checks from Phase 2 repeated unchanged to confirm the registry did not
disturb them.

**A pre-existing gap in `scripts/viewer_client.py`, found and fixed
rather than worked around.** Subscribing to `survey_spectrum` from the
CLI refused with "unknown stream(s)" -- `ALL_STREAMS` had not been
touched since before ticket 07 added those two streams, over a month
before this ticket in this project's own history. Fixed alongside
`decode_binary()`, which would have crashed on a real `survey_spectrum`
message next: type 3's header is 28 bytes (`lower_hz`/`upper_hz` before
the one float array) where types 1 and 2 are 20, and the function
assumed 20 for everything. Verified against a synthetic type-3 payload
built by hand before trusting it against the live server. `run_print()`
had the same shape of gap one level up -- any JSON message that was not
`link_health` or `command_result` was printed as a `receiver_state`,
which crashed with a `KeyError` the first time a real `survey_state`
message arrived, since that shape has no `center_hz`. All three fixes
are additive (a stream name, a header-size branch, a dispatch case) and
touch nothing this ticket's own acceptance criteria depend on, but they
were necessary to measure this ticket at all: `make bench-serve
SUBS_SERVE=survey_spectrum,...` could not run before them.

**`make bench-serve`'s improvement claim was attempted and is not
resolved** -- see the evaluation criterion above for both rounds' numbers
and why they do not support a claim either way in this environment
today. This is the one criterion Phase 3 leaves genuinely open rather
than done; everything else on the wire is confirmed directly.

**`make check` (21781 checks, 77 suites) and `make check-pipelines` both
pass, run fresh after Phase 3.** The seven-check difference from Phase
2's run is unrelated to this ticket -- no `check-*` rule reads anything
under `web/`, confirmed again by the `MISSING:`/`NOT GATED` audits, so
whatever moved it is pre-existing variance in a suite this ticket never
touches, not a regression to chase down here.

**Phase 4 (ticket 07's remaining views) is the only phase left**, and is
a separate ticket's work by this ticket's own plan -- each of FM, GSM,
ADS-B, TETRA, LTE, SRD and the two overlays becomes one `views/*.js` file
and one line in `VIEWS`, which is what the registry existing was for.
