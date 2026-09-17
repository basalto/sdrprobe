# 14 - Restructuring the web view, before it becomes what ADR-0007 already fixed once

Status: needs-triage

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

- [ ] Ticket 13 lands: `web/` source, generator, `build/viewer_page.h`,
      byte-identical output.
- [ ] `web/lib/format.js` -- `formatBytes`, `formatBitsPerSecond`, and the
      hertz/decibel spellings currently inline.
- [ ] `web/lib/chart.js` -- `dbfsToY`, `plot`, `colorFor`, axis and grid
      drawing, taking a context and a rect.
- [ ] `web/lib/table.js` -- the candidate table's row rendering, which the
      decode views will each want.
- [ ] `web/wire.js` -- message type constants and one decode entry point;
      binary headers and JSON payloads in one module.
- [ ] `web/viewer.js` -- socket, reconnect, ADR-0027 generation rule,
      subscription, tab routing, health panel.
- [ ] `web/views/scope.js`, `web/views/survey.js` -- `markup`, `streams`,
      `render(state)`, nothing else exported.
- [ ] The registry in `viewer.js`; `showTab()` mounts and dispatches rather
      than toggling two known panels.
- [ ] Subscribe line rebuilt from the active view's `streams` on every switch.
- [ ] The generator concatenates in dependency order and the order is stated
      in one place, not implied by filenames.
- [ ] `viewer_page.h`'s rationale comment survives the move (ticket 13 owns
      where it goes).

## Evaluation criteria

**Behavioural, and all of them checkable without a person:**

- [ ] With one view showing, the server sends **only** that view's streams --
      measured on the wire, by message type, not read off the subscribe line.
- [ ] Switching tabs changes the subscription within one message, and
      switching back restores it.
- [ ] A message stamped with a stale `tuning_generation` is still declined
      after the restructure. This is *"the one rule this page exists to
      prove"* (ADR-0027) and the single most likely casualty of moving the
      decode out of `ws.onmessage`.
- [ ] A dropped connection still reconnects and still shows the newest state
      rather than a backlog (ADR-0002's freshness rule, as the page
      demonstrates it).
- [ ] `make bench-serve` shows no regression: a restructure must not
      reintroduce ticket 10's or ticket 07's spin, and a subscription that
      follows the view should show an *improvement* with a single-view client.
- [ ] Ticket 11's structural check passes against the restructured page, and
      each `views/*.js` is loadable by it in isolation.
- [ ] Adding a seventh view touches `views/` and the registry line, and no
      other file.

**Structural:**

- [ ] No file in `lib/` references a socket, a message, a stream name or a
      view.
- [ ] No file in `views/` opens a socket or names a message type.
- [ ] No framework, no CDN, no npm, no bundler (ticket 01's constraint,
      unchanged).
- [ ] The native window is untouched: `make screens` structurally identical,
      `make check` unchanged in count.

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
