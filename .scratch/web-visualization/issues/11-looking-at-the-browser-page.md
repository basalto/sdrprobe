# 11 - Looking at the browser page, the way `make screens` looks at the window

Status: needs-info -- **a third avenue was built and gated, 2026-09-26**:
`check-web-layout` drives a real browser over the DevTools protocol and asks
the page about itself. It closes (b)'s hardest criterion -- the JSON-driven
text -- by a route neither (a) nor a PNG could. (a), the DOM-shim structural
check, is still not committed. See "What was built".

## The gap

`CLAUDE.md`: *"A change that draws is not finished until somebody has looked
at it."* The browser page draws. Nobody can look at it repeatably.

`make screens` does not cover it and **cannot**, as built. The chain is
`scripts/screens.sh` -> `scripts/screenshot.sh` -> `./sdrprobe --screenshot
out.png`, and all 21 recipes are `--file <capture> --view <name> --duration
N`. `--screenshot` writes raylib's last frame: a GPU/software buffer inside
this process. The browser page is a different process at the other end of a
socket, rendered by a DOM and a canvas. No `--serve` appears anywhere in that
chain, and no flag would make it appear -- there is nothing for raylib to
capture.

So `src/viewer_page.h` -- a tab bar, two panels, two charts, a candidate
table, a health panel -- has the standing this repository gives to no other
drawing: it ships, and the only way to see it is to open a browser by hand.

## What was tried in ticket 07, and what each is worth

**Headless Chromium `--screenshot`: produces a PNG, and the PNG is wrong in a
silent way.** Across four attempts and two headless modes, the capture showed
the tab bar and a live Scope trace -- the canvas -- and **never** the
JSON-driven text: the status line, the candidate rows, the health fields. A
known-shaped timing quirk between canvas repaints and DOM text reflow in
screenshot mode.

That result is the reason this is a ticket and not a one-line recipe. A naive
`chromium --headless --screenshot` entry in `screens.sh` would produce a
plausible picture with every value missing, and a reviewer comparing it
against the previous commit would see no difference, because the fields were
blank in both. **Worse than no recipe**, on this repository's own terms: a
green tick over the half it does not model.

**A Node DOM shim against a live server: worked completely.** The *actual*
extracted `page.js`, unmodified, against a real `--serve` -- `receiver_state.tab`
driving the tab switch, the status line, 37 candidate rows, the health panel,
every field correct. No browser, no screenshot timing to fight.

But it is not a picture. It asserts DOM state. It cannot see two panels drawn
on top of each other, a chart that came out blank, or a column of numbers
running off the right edge -- which is the exact list `CLAUDE.md` gives for
why `check-layout` is *"necessary and nowhere near sufficient"*, and all five
of those shipped in the raylib views.

## Two pieces of work, and they are not alternatives

**(a) A structural check -- the one that works.** Drive the real `page.js` in a
DOM shim against a real `--serve`, assert the fields. ADR-0012's shape exactly:
no window, no receiver, no person. It would have caught the subscribe-token
fault in ticket 12 by itself, and it is the only thing here that can run in
`make check`.

The awkward part is honest and should be decided rather than discovered: the
page is a C string in `src/viewer_page.h`, so the check has to extract it the
way the scratch harness did. Whether that stays an extraction or the page
becomes a real `.js` file the header embeds at build time is the design
question this ticket carries.

**(b) A picture, for `make screens`.** Settle the Chromium behaviour --
`--virtual-time-budget`, an explicit settle, or a headful capture under the
compositor `screenshot.sh` already drives -- and add a `web` recipe so a
change to `viewer_page.h` can be looked at. This is the half that answers
`CLAUDE.md`'s sentence. A DOM assertion is not looking.

Order: (a) first, because it works today and is check-shaped; (b) second,
because it needs a browser question answered before it can be trusted.

## Acceptance criteria

- [ ] A change to `viewer_page.h` that breaks a field fails something, with no
      person involved.
- [ ] `make screens` renders the browser page, or this ticket records why it
      cannot and what replaces it.
- [ ] A rendered web capture shows the JSON-driven text, not only the canvas.
      A recipe that silently omits half the page does not close this.
- [ ] Whatever is built is a committed script behind a `make` target, not a
      harness in a transcript.

## Note on the scratch harnesses

Both were written **once**, in ticket 07. `CLAUDE.md`'s rule is repetition:
*"a scratch harness written three times is a tool"*. Ticket 07's own list of
what is still open is FM and four decode views, each needing exactly this
verification -- so the second writing is already scheduled. Promote on the
second, not the third: the rule is about evidence that something is needed,
and a ticket naming the next four callers is that evidence.

## What was built, 2026-09-26 -- `check-web-layout`, a third avenue

This ticket framed the choice as (a) a DOM shim that works and cannot see,
or (b) a picture that can see and comes out blank. **There is a third
thing, and it is better than either for everything except a literal
picture**: drive a real browser over the DevTools protocol and ask the page
about itself. Real layout, so overflow, stacking and geometry are
answerable; the live DOM rather than a photograph of it, so text that did
not render cannot hide behind a canvas that did.

`scripts/web_layout.mjs`, behind `make check-web-layout`, in `CHECK_UNITS`.
It starts its own `server` over `fm_rds_tsf.bin` and its own headless
Chromium, walks every tab at every requested viewport
(`WEB_SIZES=1920x1080,1400x900,...`, one by default so the gate stays
quick), and asserts per tab: the document does not scroll, no scrollbar
exists, **exactly one view panel is laid out**, and every visible canvas's
backing store equals its laid-out box. On FM it also asserts the three
information panels are one width, one height and one row, and -- this
ticket's hardest criterion -- that the funnel's sentence, the signal rows,
the station name, the waterfall's axis and the health footer all **rendered
as text**.

**It was promoted on the second writing, which is this ticket's own rule.**
The first was ticket 07's scratch harness; the second was written during
Phase 4's FM work to answer "does this page scroll", which no fake DOM can
answer.

**It earned its place immediately.** It found two faults of the same kind
on its first run, neither visible to the Node harness: an author `display`
beats the `hidden` attribute's UA rule, so a `#panels > div { display:flex }`
rule laid out *every view's panel at once* -- three stacked, 1720 px of
content in a 757 px viewport -- and the same fault one level down had a
hidden chart wrapper eating 136 px of the waterfall's height. Mutating the
fix back out fails it six times.

### Decisions this makes, which the ticket left open

- **No Playwright and no browser-automation MCP.** CDP needs two methods
  here -- find a page target, evaluate an expression in it -- and Node's own
  `fetch` and `WebSocket` reach both, so the whole client is about sixty
  lines with no dependency. `web/` is under a standing no-framework,
  no-CDN, no-npm constraint (ticket 01, restated by 13 and 14); its own
  test harness pulling in an automation stack and a second managed Chromium
  would be that constraint held everywhere except where it is checked. An
  MCP would also cost every session's context whether or not it touches
  `web/`.
- **A missing node or chromium is a SKIP that says so**, printed on the
  suite's own line, never a silent pass -- the shape `tests/pipelines.sh`
  uses for a missing capture.
- **It appends to `CHECK_TALLY`** like every C suite, so the gate's own
  summary counts it. It did not at first, and the gate read 78 suites while
  running 79 -- a suite that does not count itself is the `NOT GATED` fault
  wearing a different coat.
- **`check-make-help` caught it before a person did.** The rule shipped
  with no `#:` line and the audit failed the gate, exactly as designed; a
  Node suite has no `check_report()` sentence for `make_help.py` to read,
  so it needs the explicit one.

### What is still open

- **(a), the DOM-shim structural check, is still not committed.** It
  remains worth having and is not replaced by this: it asserts what the
  page *decided* -- the generation rule, the reconnect, the subscribe line
  sent on a tab switch -- at a fraction of the cost and with no browser. The
  scratch harness that does this now stands at 62 checks and has been
  written twice.
- **A literal picture is still not in `make screens`.** `Page.captureScreenshot`
  is wired (`--png FILE`) and a PNG from it is trustworthy *because the same
  run asserted the text is there first*, which is what ticket 07's blank
  capture lacked. What is not done is a `screens.sh` recipe at the 1500x950
  every other screen renders at.

## Not in scope

- Retiring `make screens` or the raylib screens. ADR-0027 keeps the window
  primary; this adds a second thing to look at, and takes nothing away.

## Implementation notes -- what to take into account

**Do ticket 13 first.** The page is 307 adjacent C string literals today, so
(a)'s check would have to un-escape and extract its own subject before it
could run it -- an untested extractor between the check and the thing checked.
Ticket 13 makes `web/viewer.js` a file; this check then loads it. Attempting
(a) first means writing the extractor and then deleting it.

**For (a), the structural check.** What the scratch harness did, and what to
rebuild properly: start `./sdrprobe --serve` on a port, open a WebSocket from
Node, and run the page's own script under a minimal DOM shim -- stubs for
`document.getElementById`, `canvas.getContext` (the 2d calls can be
no-ops that count invocations), `performance.now`, `setInterval` and
`WebSocket`. Then assert what the page *decided*: `panel-survey.hidden`
follows `receiver_state.tab`, the status line matches what the server sent,
the candidate row count matches `survey_state`, and a message stamped with an
old `tuning_generation` is declined (ADR-0027's one rule this page exists to
prove).

Take into account:

- **This needs a running server, so it is not a `-lm` unit.** Its natural
  neighbour is `check-pipelines` -- the POSIX `sh` script that runs the built
  binary and greps stdout -- rather than `CHECK_UNITS`. Decide which, and
  whichever it is, put it in the list: `CLAUDE.md`'s `NOT GATED` audit exists
  because `check-signal-probe` was green, picked up by `check-touched`, and
  never run by the gate.
- **It needs node.** Nothing in this build does today. A missing node must be
  a **skip that says so**, the way `tests/pipelines.sh` guards its captures
  with `have` -- *"reports a missing capture as a skip rather than silently
  omitting coverage"*. A silent skip here is worse than no check.
- **A capture, not the receiver.** `--serve` with `--file` gives a repeatable
  spectrum; the survey half needs `--survey-range`, which now combines with
  `--serve` (ticket 07) but sweeps only on a live receiver. So the survey
  assertions may have to be driven by feeding `survey_state` directly rather
  than by running a real sweep -- decide, and say which, because a check that
  quietly needs a dongle is a check that does not run.

**For (b), the picture.** Avenues not yet tried, in the order worth trying:
`--virtual-time-budget` (Chromium's own answer to "render after the async
work"), an explicit settle between page load and capture, and a headful
capture under the compositor `scripts/screenshot.sh` already drives with
`hyprctl`. Whatever works, the recipe must assert that **text** rendered, not
just that a PNG was written -- the failure mode measured in ticket 07 is a
perfectly valid PNG with the canvas drawn and every value blank.

If none of them work, this ticket's honest outcome is to record that, and say
that (a) is what the repository has -- the same shape as ADR-0022's amendment,
where a promise that could not be kept was written down as not kept rather
than left implied.

**Sizing the screens recipe.** `scripts/screens.sh` takes width and height and
all 21 recipes render at 1500x950. A browser recipe should match, so a web
capture and a window capture of the same screen can be put side by side.
