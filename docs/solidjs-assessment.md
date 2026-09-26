# SolidJS assessment for sdrprobe's web view

## Recommendation

**Do not adopt SolidJS.** Two independent reasons, either one sufficient on
its own:

1. It reopens a decision this project has made **three times**, not a gap
   nobody noticed: ticket 05 first states it ("The page itself -- Canvas, no
   framework, no CDN.", `.scratch/web-visualization/issues/05-the-viewer-link-and-the-first-state-updates.md:20,75`),
   ticket 13 restates it while moving the page to files ("No CDN, no
   framework, no build toolchain... Nothing here introduces npm.",
   `.scratch/web-visualization/issues/13-the-page-as-files-rather-than-a-c-string.md:129-131`),
   and ticket 14's own acceptance criteria check it again ("No framework, no
   CDN, no npm, no bundler (ticket 01's constraint, unchanged).",
   `.scratch/web-visualization/issues/14-restructuring-the-web-view.md:256-257`).
   Adopting a framework here is a fourth conversation about that constraint,
   not a library upgrade.
2. Even judged purely on technical merit, there is no problem in
   `web/` today that SolidJS's actual mechanism — fine-grained DOM
   patching instead of virtual-DOM diffing — would fix. The evidence for
   that claim is below, file by file, not asserted.

If that constraint is ever deliberately reopened, the least-bad path is a
vendored, JSX-free build (`solid-js/html`'s tagged templates, not the
compiler) checked in like `vendor/raygui.h` is on the native side — see the
last section for exactly what that would and wouldn't buy.

## What SolidJS actually is

A UI library built around **signals**: `createSignal()` returns a
getter/setter pair, and `createEffect()`/`createMemo()` re-run automatically
when a signal they read changes — dependency tracking happens by which
signals a function *reads*, not a declared list. Its JSX compiler turns
markup into direct `template()`/`insert()` calls at build time, so a
changed signal patches exactly the DOM node or attribute tied to it; there
is no virtual DOM, no diff pass, and — the fact worth being precise about —
**a component body runs once**, at creation, not on every update. Runtime
is small, on the order of 7-8 KB gzipped. A JSX-free path exists
(`solid-js/html`, tagged template literals) for exactly the constraint this
project has: no compiler needed, reactive expressions are just wrapped in
arrow functions by hand. SolidJS has no canvas-specific API; canvas drawing
is always an imperative escape hatch reached from inside a signal-reading
`createEffect`, same as calling a plain function from anywhere else — this
matters directly, below.

## Where sdrprobe's web view actually spends its time

**Canvas dominates, and SolidJS offers nothing there.** The waterfall
publishes at roughly 15 rows/sec (one per acquisition block; see
`viewer_session.c:310-312`), and `web/views/scope.js`'s `drawWaterfall()`
(`:45-54`) and `drawSpectrum()` (`:28-35`) are plain
`CanvasRenderingContext2D` calls — `drawImage` to scroll, `fillRect` per
column, `plot()` traces. `web/views/survey.js`'s `drawSurveyChart()`
(`:51-73`) is the same shape. None of this is DOM; a signals graph sitting
above these functions would still end by calling exactly the same
`ctx.fillRect`/`ctx.drawImage` sequence from inside an effect instead of
from inside `render(msg)` — no patching, no diffing, no benefit, one more
layer of indirection to read through.

**The DOM-touching code is a small minority, and it's already been
hand-optimized with the exact reasoning SolidJS would formalize.**
`web/lib/table.js`'s `renderRows()` (`:8-10`) does a full
`tbody.innerHTML = ...` rebuild on every call, on the Survey candidate
table — a case SolidJS's `<For>` would turn into keyed per-row patches.
But this fires on `survey_state`, which arrives at the sweep's own pace
(well under 1 Hz in practice), not the 15 Hz waterfall, and the table's row
count is a sweep's candidate list — tens of rows, not thousands. Nothing in
this session's own measurements (ticket 08's Health panel, which reports
this page's own JS-busy percentage) has ever flagged this rebuild as a
cost worth naming.

`viewer.js`'s `renderHealth()` (`:145-160`) also does a full `.innerHTML`
rebuild, but the surrounding code already solved the problem a signals
library exists to solve, by hand: `handleState()` (`:115-139`) stores
`link_health` messages (which "arrive up to once per server block, tens of
times a second") without rendering, and a `setInterval` at `:73-80` redraws
the panel once a second. The comment at `:116-121` states the reasoning
directly: *"a full innerHTML rebuild on every one of them is JS busy time
this page does not need to spend... spending less time here is less time
not reading the socket, which is less backpressure this page itself
causes."* That is the same insight SolidJS's fine-grained updates are sold
on — update only as often as the data meaningfully changes — arrived at
here with an interval and eleven lines of comment instead of a dependency.

**The dispatch that would normally justify a component tree already
doesn't need one.** `viewer.js`'s `ws.onmessage` (`:96-112`) decodes one
message, then calls exactly one function: `activeView.render(msg)`
(`:108`) — the one view currently showing, chosen once at subscribe time.
There is no tree of components for a diff algorithm to walk and prune,
because there is no tree: `VIEWS` (`:11`) is a flat array of two
today, `web/views/*.js` growing by one file per future decode view
(ticket 07's remaining list). SolidJS's whole value proposition — "skip
re-rendering components whose inputs didn't change" — answers a problem
that a two-line direct dispatch does not have in the first place.

## What SolidJS would concretely change, and what it wouldn't

| Surface | Today | With SolidJS |
|---|---|---|
| Waterfall / spectrum / survey canvases | imperative `ctx.*` calls from `render(msg)` | identical imperative `ctx.*` calls from inside a `createEffect` — no change |
| Survey candidate table | `innerHTML` rebuild, tens of rows, sub-1Hz | `<For>` keyed patches — real technique, no measured problem to apply it to |
| Health panel | `innerHTML` rebuild, hand-throttled to 1/sec with documented reasoning | a `createMemo`/effect could replace the `setInterval` coalescing — same behavior, more machinery |
| Message → view dispatch | one direct function call, no tree | a component tree SolidJS would then avoid re-rendering — solves a cost this code doesn't pay |
| Bundle / dependency surface | zero: every file is plain JS, concatenated by `scripts/embed_web.py` | a new runtime to vendor, pin, and update forever |

## If this is ever revisited anyway

The only way to do it without contradicting the standing decision outright
is to vendor a prebuilt, JSX-free build the way `vendor/raygui.h` is
vendored on the native side: `solid-js`'s compiled ESM runtime plus
`solid-js/html` (the tagged-template renderer), checked into `web/`, loaded
via a plain `<script type="module">` with no npm, no CDN fetch at runtime,
and no build step. That keeps the letter of "no build toolchain, no CDN".

But it gives up exactly the feature that makes SolidJS worth using.
`solid-js/html`'s own documentation is explicit about the cost: it needs a
**larger, non-tree-shakeable runtime**, reactive expressions must be
**manually wrapped in arrow functions** (the compiler normally does this
inference), event handlers need explicit function arguments, and refs are
callback-only. That is JSX's ergonomics traded away while keeping SolidJS's
~7-8 KB of runtime weight and a dependency that has to be re-vendored by
hand on every upstream update — a strictly worse trade than the
`markup`/`render(msg)` shape this page already has, where every view is a
plain object literal (`web/views/scope.js:56-72`,
`web/views/survey.js:75-92`) with nothing to install, pin, or update.

## What not to adopt, and why

- **SolidJS itself.** Reopens a three-times-made decision, for a class of
  problem (component-tree re-render cost, large DOM diffing) this page
  does not have — canvas is the hot path and DOM updates are already
  either small, sub-1Hz, or hand-throttled with documented reasoning.
- **Any UI framework, on the same reasoning.** `web/`'s entire view layer
  is `lib/` (three files, `format.js`/`chart.js`/`table.js`) plus a
  handful of `views/*.js`, each following one disciplined shape
  (`{id, label, tab, streams, markup, render(msg)}`, ADR-0007's layering
  carried onto the frontend). A framework's conventions would duplicate
  a convention this codebase already enforces by hand, at a fraction of
  the size, with nothing to vendor.
