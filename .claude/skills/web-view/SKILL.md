---
name: web-view
description: Build or change a view in the browser Viewer (web/). Use when adding one of ticket 07's remaining views, editing web/views/*.js, web/lib/*.js, viewer.js, wire.js or viewer.html, changing what a Viewer stream carries, or when a browser panel does not look or behave like the window's.
---

# The browser Viewer

`web/` is a second reader of the same measurements the raylib window draws.
ADR-0027 keeps the window primary, which makes it the thing a web view is a
view *of* — not a separate product with its own opinions.

Four views are still to come (ADS-B, TETRA, LTE, SRD — ticket 07); GSM
landed 2026-09-27, so everything below has now been done twice past FM.

## Ask before deciding

**When a choice here has more than one defensible answer, ask — with the
options spelled out — rather than picking and reporting.** Several of the
decisions below were made once, wrongly, and redone:

- The FM view was first built with its own arrangement (a chart, then
  stacked panels) instead of mirroring the window's. It had to be redone.
  The question "how closely should this match the window?" has at least
  three real answers and the operator holds it, not you.

Ask with `AskUserQuestion`, one question per turn, concrete options, a
preview where a layout is being chosen. Questions worth asking rather than
answering yourself:

- **Fidelity.** Mirror the window's arrangement, adapt it for a browser, or
  reproduce it pixel-for-pixel? (Default, and what FM settled on: mirror
  it.)
- **What travels.** A measurement the window computes while drawing — does
  it move into the view model, or stay window-only?
- **A new stream, or a field on an existing one.** Cheap either way; the
  answer decides whether `wire.js` changes.
- **What a panel does when it does not fit.** Scroll inside itself, drop
  rows, or shrink the chart?

Do **not** ask about anything this file already settles. Re-asking a
decided question is its own kind of noise.

## The shape of a view

One file in `web/views/`, exporting exactly one name, built by an IIFE:

```js
const GsmView = (function () {
  let els = null;
  function elements() { /* memoised getElementById, see below */ }
  return {
    id: 'gsm',          // the `view <id>` command, and the DOM id suffix
    label: 'GSM',       // the tab button's text
    streams: ['gsm_state'],   // beyond receiver_state/link_health
    markup: '...',            // the panel's HTML, mounted by the shell
    render(msg) { /* draw from what wire.js decoded */ },
    resize() { /* match canvas backing stores to their laid-out boxes */ },
  };
})();
```

**The IIFE is not decoration.** Every file under `web/` is concatenated into
one `<script>`, so a top-level `const` is a global shared with every other
file. The first draft of `scope.js` and `survey.js` both declared `let els =
null;` at top level — a straight collision the moment two view files exist.
Only the returned object's name may escape.

**`elements()` must be lazy.** `markup` is not in the document when a view
file runs: `viewer.js` is concatenated last and inserts it at
`mountViews()`. A `getElementById` at top level returns null.

### Layer rules (ADR-0007, restated for JavaScript)

- `web/lib/` takes **plain data and geometry** — a context, an array, a rect,
  a style. It never sees a socket, a message, a stream name or a view.
  Today: `format.js`, `chart.js` (`dbfsToY`, `plot`, `colorFor`,
  `fitCanvas`, `measure`), `waterfall.js`, `table.js` (`renderRows`).
- `web/views/` never opens a socket and never names a message type.
- `web/viewer.js` owns everything that is not drawing: the socket, the
  reconnect, ADR-0027's generation rule, the registry, tab routing, the
  subscription, the health footer. It is the only file that reads
  `wire.js`'s constants.

## Adding a view: which files, and no others

Three, and the prediction is worth holding yourself to:

1. `web/views/<name>.js` — the module.
2. `web/viewer.js` — one entry in `VIEWS`.
3. `scripts/embed_web.py` — one entry in `JS_ORDER`, which is deliberately
   explicit rather than a glob.

A **fourth** file is a signal, not automatically a fault. FM touched
`wire.js` because it added a *binary* message type. A view whose state is
JSON needs nothing there — every JSON message is already `kind: 'state'`.
If you are editing `viewer.html`, stop and ask whether it belongs in the
view's own markup instead; FM keeps its colours and its flex layout inline
for exactly this reason.

**The prediction held for GSM**: exactly those three, no `wire.js`, no
`viewer.html`. What it does *not* cover, and did not claim to, is the C side
(four more files: the model, its builder, its check and the wire) or the
tooling -- GSM also taught `scripts/viewer_client.py` to print `gsm_state`
and `scripts/web_layout.mjs` to drive a non-FM capture. Count the page's
files against the three; those others are their own thing.

## The C side is where a decision belongs

**A web view must not compute anything the window decides.** If a view
needs a field the view model does not carry, add it to the view model,
where `check-*` can reach it (ADR-0012) — never to the JavaScript.

The pattern, as FM did it:

- **The contract and the builder live in different layers** (ADR-0028):
  `src/model/<tech>_view_model.h` is plain fields, no raylib type, no I/O;
  `src/runtime/<tech>_view_model.c` is the builder, where reading
  `struct app` is allowed and expected. `check-<tech>-view-model` links
  `-lm` alone with no raylib flags -- all five do now, FM included.
- **Build it from the state it reads, not from `const struct app *`.** The
  builder takes the two or three structs it is about; the suite then sets
  those rather than filling nine megabytes.
- **`check-layers` will refuse a model that reaches up.** GSM's header
  wanted `SCAN_ARFCN_LAST` from `runtime/scan_plan.h` and could not have
  it. The idiom for that is `input_route.h`'s: mirror the constant in the
  model, and have the builder -- which sees both -- assert at compile time
  that they still agree, so a band that grew is a build error rather than a
  truncated chart.
- **Anything the drawing *chose*** — which of several sentences, which
  emphasis, which mark — moves into the model as a value.
  `fm_view_model_reading()` picks one of five sentences and a
  `enum fm_reading_tone`; the drawing only picks a colour for a verdict it
  was handed. `survey_mark_of()` (`src/model/survey_mark.h`) is the same idea for the
  survey, and `gsm_sch_reading_name()` the same again.
- The window's own drawing then reads the model too, so there is one
  decision rather than two that agree today.

Register the new header in `APP_HDR` and the new rule in `CHECK_UNITS`, and
re-run both audits in `CLAUDE.md`. A suite that is green and never run is
the failure these exist for.

## The wire

- **JSON state stream** (the usual case): add to `enum viewer_stream` and
  `stream_names[]` in `viewer_link.c` (one table, matched by the subscribe
  parser — do not add a hand-written branch), a `viewer_link_publish_*`,
  and a call in `viewer_session.c` gated on `spectrum_updated`. **That gate
  is load-bearing**: publishing every loop iteration measured at 234216
  messages in 10 seconds, because publishing queues rather than sends.
- **Binary array stream**: also a `enum viewer_message_type`, and `wire.js`.
  If the array does not sit on the receiver's own frequency grid, use
  `VIEWER_RANGE_HEADER_BYTES` and `publish_range_binary()` — it carries
  `lower_hz`/`upper_hz` and both the survey sweep and the FM multiplex go
  through it.
- **A decode view is not a tab.** `view <name>` for one is a `set_decode()`
  *then* a `set_tab()`, in that order — switching the tab first enters
  whichever decode kind is already recorded and leaves it again on the way
  past, retuning twice. Add the name to `viewer_screens[]` in
  `viewer_command.c` and to `enum viewer_screen`.
- `receiver_state` carries one **`screen` name**, which is the view's own
  `id`; `viewForState()` matches on it. It used to carry `tab` and `decode`
  as integers, which is what drew the survey's marks swapped
  (`layer-boundaries/03`).
- **`check-viewer-link` keeps its own list of stream names, twice**, and it
  is not duplication — it is the second statement that makes the first a
  check. Adding a stream means adding the name in three places; the suite
  fails by name until you do, which is how GSM's was caught.

`link_health` names **every** stream, walked from `stream_names[]`, and the
footer shows whichever the page is subscribed to plus any that has dropped.

**Do not read a fresh stream's counts off a screenshot.** `link_health`
arrives about once a second and the counts are per connection, so a tab
selected two seconds before the capture shows the new stream at `0/0` while
its data is plainly rendering above. That looked exactly like a broken
publisher for a while; the server's own disconnect tally (115 sent) against
the client's received count (119) is what settled it.

## How a view divides its height

**The charts are what a reader is here for.** The page's chrome is 12px and
the information tables 11px, and that was measured rather than chosen:
before it, **224 of 900 pixels** went to the title, the tab bar, the hud and
the health footer, and GSM's waterfall came out at 195 of a 676-pixel panel
-- 29%.

A view's own split is then weights on the flex children, and the chart takes
more than half:

| view | the split | chart at 1400x900 |
|---|---|---|
| scope | spectrum and waterfall, even | 342 each |
| survey | chart `2`, candidate table `1` | 463 |
| fm | waterfall, then the three panels | 481 |
| gsm | waterfall `3`, channel scan `1` | 372 |

Two of those were even splits and should not have been: the survey's table
is **empty** on a capture and scrolls when it is not, and GSM's channel scan
says only "needs a live receiver" unless a receiver is attached. A panel
that is usually blank is not owed half the screen.

**Both floors are gated**, and measured across four viewports rather than
picked: the panel is at least 65% of the viewport, and the biggest chart at
least 25% of the panel -- rising to 40% wherever the panel is 500px or more,
which is where there is actually room to divide. GSM at 1024x600 is the
bottom of that range at 33%, and inherently: its two readout lines and two
information panels cost about 200 fixed pixels of a 434-pixel panel whatever
the chart does.

**65% and not 70 for the first floor**, because the health footer is one
line or two depending on whether any stream has dropped yet, and at 1024x600
that is the difference between 420 and 418 pixels. A threshold a dropped
message can cross is measuring the footer.

## Traps that have actually bitten

Each of these cost real time in this repository.

- **An author `display` beats the `hidden` attribute's UA rule.** A
  `display:flex` rule on `#panels > div` laid out *every* view's panel at
  once — three stacked, 1720 px of content in a 757 px viewport. The same
  fault one level down had a hidden chart wrapper eating 136 px. If an
  element carries an inline `display`, toggle `style.display` alongside
  `hidden`, not instead of it.
- **`min-height:0` is what lets a flex child be smaller than its content.**
  Without it a canvas or a long table pushes the column past the viewport
  and the scrollbar returns.
- **A canvas's backing store must equal its laid-out box.** Use `measure()`
  then `fitCanvas()`. Computing a size from the viewport instead is a second
  opinion about a layout CSS already settled, and the two disagree by a
  scrollbar's width.
- **Resizing a canvas clears it.** A waterfall's canvas is its picture, so
  keep rows (`lib/waterfall.js`) and redraw only when `fitCanvas()` reports
  the geometry changed. Do **not** redraw the whole history per arriving
  row: that was measured at 95%+ of this page's JS busy time.
- **Colours belong to the window.** Take them from the view's own `Color`
  constants in `src/gui/view_*.c` — `panel_edge`, `panel_caption`, `row_label`,
  `row_value`, `row_good`, `row_weak` — as the hex of the exact RGB. A panel
  that is nearly the window's colour is one a reader looks at twice.
- **The subscribe parser and the screen names are tables.** Ticket 07 found
  the hand-written version two names short: a client subscribed to
  `survey_spectrum` received nothing, silently, with no refusal.
- **A wrapper needs `flex:1 1 0` as much as the canvas inside it does.**
  The zero basis was put on the canvases and left `auto` on the divs around
  them, which is the same feedback one level up and converges only where
  there is slack. Every chart wrapper is `1 1 0` now.
- **`ResizeObserver` on the container misses a change inside it.** An axis
  label wrapping to two lines at a narrower viewport moves a chart's box
  without moving `#panels`', so the observer never fires. Observe the
  canvases too -- safe, because with a zero basis a backing store does not
  feed its own layout.
- **A view that yields nothing overflows.** GSM's first layout gave the
  channel-power canvas a fixed `flex:0 0 120px` beside a waterfall and two
  readout lines, and the panel scrolled by 121px at 1400x900. Both charts
  are `flex:1 1 0` now, the waterfall weighted `3`. Anything that cannot
  shrink has to be small enough that everything else can.
  **And check the weight landed**: the edit that was supposed to set it the
  first time silently did not -- its replace target had a trailing space --
  so a commit claimed a weighting the page did not have and the waterfall
  stayed small for another day. Read the markup back, or measure the box.
- **Never index a table by an enum's integer.** The survey's mark crossed the
  wire as `enum sdrgui_peak_mark`'s ordinal and `views/survey.js` re-declared
  the order wrong, so the browser drew receiver-like and empty candidates
  swapped -- the pair `CLAUDE.md` says a reader acts on -- and every check
  stayed green, because nothing checks how the browser reads a number
  (`web-visualization/15`). Send enums **by name**, and key the browser's
  tables by name. **Every enum on the wire does now** -- `shape`, `mark`,
  `seen`, `reading_tone`, and GSM's `sch` and `bcch` -- and `tab`/`decode`
  do not travel at all, having been replaced by one `screen` name. There is
  no remaining example of the bad shape to copy, which is why it is written
  down here instead.

## Verifying: which tool answers which question

Three, and they are not interchangeable.

**`make check-web-layout`** — a real browser over the DevTools protocol.
The only thing that can answer *does this page scroll, do these panels line
up, did the text render*. Scrolling and stacking are properties of a layout
engine.

```sh
make check-web-layout                                   # the gate's one size
make check-web-layout WEB_SIZES=1920x1080,1280x720,1024x600
node scripts/web_layout.mjs --png /tmp/page.png         # and a PNG to look at
node scripts/web_layout.mjs --tab gsm --file testfiles/gsm_arfcn_69.bin \
    --rate 2000000 --extra "--arfcn 69" --png /tmp/gsm.png
```

A PNG from it is worth trusting *because the same run already asserted the
text is there* — ticket 07's headless screenshot showed the canvas and left
every value blank, which is worse than no picture.

**A Node DOM shim against a live server** — the real page's bytes in a fake
DOM, driven by a real WebSocket. Answers *what did the page decide*: the
generation rule, the reconnect, the subscribe line sent on a tab switch.
Fast, no browser. **Structurally blind to layout.** Still a scratch harness;
ticket 11's (a) is to commit it.

**The window, for comparison.** `make screens NAMES="<view>"` renders the
raylib screen the web view mirrors. Three cautions, the third measured on
GSM: a comparison across a build is a comparison at two machine *loads*
unless both sides are rendered warm — a cold run right after a full compile
processes fewer blocks and reads as a regression. A waterfall makes the PNG
non-reproducible byte-for-byte. And **check that before concluding
anything**: two renders from the *same* binary differ on any screen with a
waterfall, so a byte comparison there proves nothing in either direction.
What does settle it is `git status` — if no file under `src/gui/` changed,
the window's screen cannot have — plus reading the PNG and checking the
values.

**Drive it over the right capture.** `scripts/web_layout.mjs` takes
`--file`, `--rate`, `--freq` and `--extra` ("--arfcn 69"); played at the
wrong rate a capture decodes nothing and the page shows a correct layout
full of "awaiting", which is a picture that proves the layout and nothing
else. Its server duration allows for the FM tab's 30-second wait for the
station name — over a capture that is not the FM one, that wait is spent in
full every run, and a budget that ignored it had the server exiting before
the last tab was reached.

Whatever you build, drive it against a real server over a capture:

```sh
./sdrprobe web --no-browser --file testfiles/fm_rds_tsf.bin --sample-rate 2048000 \
    --frequency 89.5M --serve-port 8790 --duration 60
python3 scripts/viewer_client.py --port 8790 --subscribe fm_state \
    --send 'view fm' --count 40
```

**When a harness assertion fails after you changed markup, suspect the
assertion first.** Three did in one session — they matched a bare
`<td>N</td>` that had gained a style, drove `resize()` through a viewport it
no longer reads, and counted a CSS property across every view's markup. All
three were stale claims, not regressions. The `check-claims` skill is the
long form.

## No npm, no framework, no CDN — and no automation stack either

Ticket 01's constraint, restated by 13 and 14 and reaffirmed against
SolidJS. It extends to the tooling: `scripts/web_layout.mjs` speaks CDP with
Node's own `fetch` and `WebSocket` in about sixty lines rather than pulling
in Playwright and a second managed browser. A test harness that breaks the
constraint is the constraint held everywhere except where it is checked.

## Keeping this file true

**Update it when a view lands, and say what changed.** A skill is
instructions that will be followed without being re-read, so a silent edit
changes how this repository is worked on. After each of the remaining views:

- Did a rule here turn out to be wrong, or to need a caveat? Correct it —
  a wrong claim in a skill is worse than a missing one, because it is
  trusted.
- Did something cost time that this file does not mention? Add it, with the
  evidence attached rather than as advice.
- Did the "three files, and a fourth is a signal" prediction hold? If a view
  touched more, either the prediction or the architecture is wrong, and
  ticket 14 wants to know which.
- Name the change in your reply: which section, what moved, and what in the
  session was the evidence.
