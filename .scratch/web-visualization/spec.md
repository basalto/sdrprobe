# Web visualization

Explore a browser visualization for sdrprobe without weakening the native
receiver, DSP and testability boundaries. The first decision is where the
network boundary belongs: after acquisition as raw I/Q, after DSP as derived
display state, or around an Emscripten build of most of the application.

**Decided 2026-09-16.** The boundary is after DSP: the native process keeps
acquisition, receiver control, DSP and persistence, and serves a **Viewer
link** on loopback carrying **State updates** out and **Viewer commands** in.
The seam both frontends share is a **view model** -- plain data saying what a
screen shows -- so the raylib window and a browser Viewer are alternative
readers of one object rather than two presentations kept in step. They are not
run at the same time. ADR-0027 and its amendment carry the reasoning;
`issues/01-*` carries the decisions and the two claims of its own that were
measured wrong.

## Constraints

- Preserve ADR-0002's freshness rule: a slow visualizer drops obsolete frames
  rather than accumulating latency.
- Keep receiver control transactional and owned by the native runtime unless
  a browser-specific backend proves otherwise.
- Send domain measurements and events, not raylib draw calls or `struct app`.
- Keep control outcomes reliable and ordered; visualization frames may be
  replaceable.
- Do not make WebUSB-only browser support the primary product path.
- Do not introduce a uniform technology interface solely for serialization.

## Settled

- **Remote access**: loopback only. The bind address is the authorization
  boundary, which is what lets a Viewer command carry no credential.
- **Capture files in the browser**: not on this path, and not declined --
  capture analysis in WebAssembly needs no receiver, no stream and no server,
  shares no code with the Viewer link, and remains its own decision.
- **The raylib presentation**: kept and primary, migrating onto the shared
  view model view by view. Retiring it is a further amendment to ADR-0027 and
  would owe an accounting of `check-layout`, the `*_layout.h` headers,
  `panel_rows.h` and `make screens`, none of which have web equivalents.
- **WebUSB**: no. Chromium-only, and a new backend rather than a recompile.

## Still open

- Subscription-driven computation. Ticket 09 makes the Viewer link's aggregate
  demand reach frame advancement; today subscriptions suppress sends only,
  after the Scope work has already run.
- The input half of the seam. 161 raylib input call sites, hit-testing done
  inline against rectangles that exist only while drawing. A Viewer command is
  not a click, so nothing on this path is blocked by it.
- Whether more than one Viewer may send commands at once.

## Tickets

1. `issues/01-stream-derived-state-to-a-web-client.md`
2. `issues/02-one-advance-step-the-window-and-headless-both-drive.md`
3. `issues/03-the-scope-view-model-and-its-first-reader.md`
4. `issues/04-a-websocket-server-with-nothing-wired-to-it.md`
5. `issues/05-the-viewer-link-and-the-first-state-updates.md`
6. `issues/06-viewer-commands.md`
7. `issues/07-migrating-the-remaining-views.md`
8. `issues/08-viewer-link-health.md`
9. `issues/09-subscriptions-drive-scope-computation.md`
