# 07 - Migrating the remaining views

Status: needs-triage -- **Survey is done** (2026-09-17), navigation included; **FM is done** (2026-09-26); **GSM is done** (2026-09-27); ADS-B, TETRA, LTE and SRD remain, then the two overlays.

## Goal

Repeat ticket 03 for the views that are not the Scope, one at a time, until
the view model is the seam everywhere rather than on one screen.

This is a tracking ticket, not a unit of work. Each view is its own change,
its own screenshot comparison and its own commit. **Any of them may be
declined**: a view nobody wants in a browser is a view that keeps reading
`struct app`, and that is a legitimate resting place rather than a debt.

## Order, and why

The cheap and useful ones first: **survey**, because a sweep is the thing most
worth watching from elsewhere and its record is already a plain struct
(`src/survey_record.c`, built for exactly this reason); then **FM**, which is
small; then the decode views -- GSM, ADS-B, TETRA, LTE, SRD -- whose sessions
already produce plain results. The overlays last: settings and calibration are
mostly widgets and typed input, which is the input half of the seam and is not
solved.

## What this is not

Not a rewrite of the drawing. The views bypass `sdrgui.h`'s components **174
times** with direct raylib calls, 104 of them bare `DrawText`. Moving those
into components is a separate and optional cleanup; this ticket only moves the
*data* behind a view model.

## The input half is unsolved and is not this ticket

161 raylib input call sites in the views and overlays -- 73 `IsKeyPressed`, 33
`GetMousePosition`, 24 `IsMouseButtonPressed`, 14 `CheckCollisionPointRec`, 11
`GetCharPressed`. Immediate mode entangles input with layout by construction:
hit-testing runs against rectangles that exist only during drawing. A Viewer
sending commands does not need this solved, because a command is not a click.
A browser reproducing the window's *interactions* would need it, and that is a
different ticket nobody has written.

## Acceptance criteria

Per view, not for the ticket as a whole:

- [ ] The view's data comes from a view model checkable with `-lm` alone.
- [ ] `make screens NAMES="<view>"` is unchanged against the previous commit.
- [ ] `make check` and `tests/pipelines.sh` unchanged.
- [ ] The view model carries no raylib type.

## Comments

**2026-09-16** -- A first, narrower step on the survey, not the full
ticket-03-style migration this ticket describes. Building the survey's
candidate list (`draw_peak_list()`) and the chart's per-peak marks
(`draw_survey()`) had drifted into computing the same decision twice --
carrier lookup, suspicion flags, shape, site-history mark -- line for
line. `src/survey_view_model.{c,h}` (commit `04c9dd0`) pulls that one
decision out into `struct survey_candidate_view`, checkable with `-lm`
alone (`check-survey-view-model`), and both drawings now read it instead
of recomputing it.

**What this does not do, and the acceptance criteria above still want**:
the survey chart's own data -- `ss->power`, `bins`, the sweep's step and
status, the drag/zoom window -- still comes straight out of
`struct app` in `draw_survey()`, the same as before. A faithful repeat of
ticket 03 would put that in the view model too, the way
`scope_view_model.h` carries the Scope's whole spectrum and waterfall
row rather than only its candidate marks. This step was worth taking on
its own because the duplication it fixes was a real, already-diverging
decision (ADR-0012), not because it closes the survey's box above.

## Not in scope

- Retiring the raylib window. ADR-0027 records that it remains the primary
  presentation; if that changes, the ADR is amended first and the cost is
  named -- ADR-0012's windowless checks, the `*_layout.h` headers,
  `check-layout`, `panel_rows.h` and `make screens` are all raylib-shaped and
  have no web equivalent.

## Done, 2026-09-17 -- Survey's view model finished, and the ticket this ticket said did not exist yet

An operator asked why the browser could not reach the Survey tab at all, and
the honest answer traced to two separate gaps: **the earlier comment's own
"what this does not do" list**, and this ticket's own "Not in scope"
section naming the real blocker as *"a different ticket nobody has
written"* -- a Viewer command that names a screen. Both are closed now,
together, because neither alone would have let anyone see a sweep from a
browser.

**Survey's view model is complete**, not narrowed. `struct survey_view_model`
now carries `sweeping`, `status` (the window's own line, copied verbatim),
`lower_hz`/`upper_hz`, and `power[SURVEY_VIEW_MODEL_MAX_BINS]` alongside the
candidate list `04c9dd0` already pulled out -- everything `draw_survey()`
reads from `struct app` that a remote reader would also need. Capped at
`SURVEY_BINS` (8192) rather than allocated, so a fixed-size wire message
never has to ask how large this sweep's range was; the cap is checked
directly (`check-survey-view-model`), and dropping it live turned into a
stack-smashing crash on the first mutation run, which is as decisively as a
check can say a clamp is load-bearing.

**The navigation ticket exists now: `view <name>`.** `viewer_command.h`
gained `VIEWER_COMMAND_VIEW` and `enum viewer_screen` (`scope`, `survey`
today; a third is one more name and one more `set_tab()` branch, not a new
command); `viewer_session_handle_command()` calls the *same* `set_tab()`
every tab-bar click already goes through, not a headless shortcut around
it. Two new streams carry what the Survey tab needs over the wire:
`survey_spectrum` (binary, `VIEWER_MESSAGE_SURVEY_SPECTRUM`, a wider header
carrying `lower_hz`/`upper_hz` because a survey's spectrum has no fixed grid
the way the Scope's does) and `survey_state` (JSON: status, sweeping, the
candidate list, each candidate's mark named through
`sdrgui_survey_peak_mark()` rather than re-decided browser-side). The
browser page gained a tab bar, a Survey panel with its own chart and
candidate table, and `receiver_state.tab` so a reconnecting or
second-opinion Viewer shows the tab the receiver is actually on rather than
whatever it last clicked.

**`--survey-range` and `--serve` combine now**, narrowed rather than
reopened wholesale: the ticket-01 exclusion listed `survey_seen`, which was
too broad the day it was written and stayed that way until this ticket
needed the distinction -- `--survey` (`survey_report`, the one-shot headless
printer) is still its own run and still refused, but a *range* alone seeds
the Survey tab's sweep the moment `view survey` selects it, through the
same `view_survey_enter()` every windowed launch already reads it from. A
second, older, unrelated refusal (`--headless && survey_seen`, predating
both this ticket and ticket 01) needed the identical narrowing for the
identical reason -- caught only by trying the live combination and watching
it refuse.

### Three real bugs, found only by driving it live

**A clock origin mismatch, not the `GetTime()` fault this file already
fixed once.** `set_tab()`'s survey branch needed `now`
(`frame_advance.h`'s own rule, and the reason six functions in
`view_survey.c` were threaded to take it explicitly rather than call
`GetTime()`, which is exactly `0.0` before `InitWindow()` -- confirmed
empirically, not assumed). The *new* fault was different in kind: the
Viewer command handler computed `now` as a raw `monotonic_seconds()`
(absolute host uptime), while `viewer_session_run()`'s own loop computes it
relative to a `started` baseline. `survey_start()` stamped
`step_started_at` from the handler's huge absolute value; every later tick
measured elapsed time against the loop's small relative one, so
`now - step_started_at` was deeply negative and `survey_step_phase_at()`
read that as "still settling" forever. One retune happened, ever, and the
sweep never advanced -- found by running it live for 75 seconds and
counting exactly one "took" line where thirteen were expected. Fixed with a
shared, file-static clock origin the handler and the loop both read.

**A publish spin, the same shape ticket 10 fixed for `receiver_state`
before this ticket existed, reintroduced for the two new streams.**
Publishing `survey_spectrum`/`survey_state` unconditionally every loop
iteration -- reasoned as "a browser switching tabs mid-session should not
wait out a block" -- measured at **234216 messages in 10 seconds**, because
`viewer_link_publish_*()` queues rather than sends and an unconditional
republish keeps `select()` always returning at once, exactly ticket 10's
finding. Fixed by gating both, like `spectrum`/`waterfall_row`, on
`spectrum_updated` -- bounding it to the block rate (confirmed: 151
messages in 10 seconds afterward, ~15.1/s).

**A subscribe token list two names short.** `handle_subscribe_line()` is a
hand-matched parser with one `else if` per stream name and no list to
audit it against; it had no branch for `survey_spectrum` or
`survey_state`, so a client subscribing to them received nothing, silently
-- no refusal, no error, just an empty stream. Found by a live client
reading zero messages after correctly formatted subscribe and publish
calls on both ends. The same file's debug log for a client's subscription
list had a second, adjacent version of the identical class of bug: a
64-byte summary buffer, sized for the original five stream names, silently
truncating mid-word the moment two more existed to list -- `subscribed:
spectrum waterfall receiver_state link_health survey_spectrum s`. Both
fixed; neither had a check that could have caught it, because both are
string tables with no enumeration checked against `VIEWER_STREAM_COUNT`.

**Verified live, twice over.** A raw WebSocket client drove the whole
loop -- `view survey`, a real retune, thirteen real sweep steps, 36-37 real
FM broadcast candidates with real frequencies and levels -- and separately,
the *actual* extracted browser JavaScript was run unmodified against the
real server through a minimal Node DOM shim (no browser, no screenshot
timing to fight), confirming `receiver_state.tab` correctly drives the tab
switch, the status line, the candidate table and the health panel all
render. A headless-Chromium `--screenshot` capture of the same page showed
the tab bar and a live Scope trace but never the JSON-driven text updates
across four attempts and two headless modes -- a known-shaped Chromium
timing quirk between canvas repaints and DOM text reflow in screenshot
mode, not a defect in this page: the Node harness runs the identical,
unmodified script against a real connection and shows every field
updating correctly.

`make check`: 21781 checks in 77 suites, no failures. `make screens
NAMES="survey"` is unchanged -- this Viewer work touches no drawing.

## Done, 2026-09-26 -- FM, the first through the Phase 4 registry

Three commits (`src/fm_view_model.{c,h}` + `check-fm-view-model`; the two
wire streams; `web/views/fm.js`), with the detail in
`14-restructuring-the-web-view.md`'s own Phase 4 comment. Against this
ticket's four per-view acceptance criteria:

- [x] **The view's data comes from a view model checkable with `-lm`
      alone.** `check-fm-view-model`, 58 checks, 78 suites in the gate.
- [x] **`make screens NAMES="fm"` is unchanged against the previous
      commit** -- byte-identical, though establishing that took a second
      attempt: the first comparison was against a baseline rendered
      immediately after a full compile, which processed fewer blocks in
      its fixed duration and read as a ~250-pixel regression. Rendered
      warm on both sides, zero pixels differ.
- [x] **`make check` and `tests/pipelines.sh` unchanged.** 21944 checks,
      no failures.
- [x] **The view model carries no raylib type.**

Two things FM turned up that the survey did not, and that the four
remaining decode views will each meet:

- **A decode view is not a tab.** `view fm` is TAB_DECODE plus DECODE_FM,
  so it needs a `set_decode()` before its `set_tab()`, and
  `receiver_state` had to gain `decode` for a reconnecting Viewer to tell
  six screens apart on one tab. That is now done once, for all of them.
- **A drawing mode was gating a computation.** The multiplex spectrum was
  computed only when the window's "Show charts" toggle was on, so the
  `fm_spectrum` stream published nothing at all under `server`. Worth
  looking for in each remaining view: anything a chart needs that is
  computed inside an `analysis_mode` branch has the same hole.

### What is still open

- The four remaining decode views (GSM, ADS-B, TETRA, LTE, SRD), each its
  own view model and its own commit, per this ticket's own ordering.
- The overlays (Settings, Calibration) -- typed input, last, unsolved.
- A `sweep <range>` Viewer *command* was deliberately not built. Seeding a
  sweep stays a command-line concern (`--survey-range`, ADR-0012), the same
  way every other screen is reachable, rather than a second, interactive
  control surface alongside it -- consistent with `view <name>` naming a
  screen and not deciding what it shows.

**2026-09-26** -- two things for whoever takes the next view, from the
architecture review in `.scratch/layer-boundaries/`. Build its view model
from the state it reads rather than `const struct app *` (that spec's ticket
03 converts the three that exist; a new one should not need converting). And
send every enum by name: the survey's mark crossed as an ordinal, the browser
re-declared the order wrong, and receiver-like and empty candidates have been
drawn swapped since this ticket's Survey work (`15-*`).

## Done, 2026-09-27 -- GSM

Three files, as the skill predicts: `web/views/gsm.js`, one `VIEWS` entry in
`web/viewer.js`, one `JS_ORDER` entry in `scripts/embed_web.py`. Plus the C
side -- `src/model/gsm_view_model.h` with its builder in
`src/runtime/gsm_view_model.c` (ADR-0028: a contract may not reach up, so the
builder lives where it can read `struct app`), `check-gsm-view-model` at 41
checks linking `-lm` alone, and a `gsm_state` JSON stream.

**What travels, decided by the operator rather than picked**: the two
readouts, the header's statistics, the 124-channel power scan, and the
waterfall. No new binary stream -- the waterfall the GSM screen draws is the
*same* rows `waterfall` already carries; only the axis differs, and which
ARFCN a frequency is is a decision, so it travels in the JSON. The SCH
constellation was declined as the piece least useful from elsewhere.

### The decision this view was really about

`view_gsm.c` chose **four possible SCH sentences and three possible BCCH
ones** inside `DrawText` calls, each with a different set of fields attached.
That is the shape ADR-0012 asks for a name, and the browser would have had to
decide it again. Both are `enum`s with names now -- `idle`/`searching`/
`recording`/`decoded` and `none`/`waiting`/`missed`/`read` -- and **the window
reads the same model**, so there is one decision rather than two that agree
today.

The distinction worth keeping is `missed` against `waiting`: the broadcast
channel occupies frames 2 to 5 of the 51-multiframe, so only the SCH at frame
1 is followed by one. Four decodes in five have nothing due and nothing
wrong; the fifth is a block that should have survived. Collapsed into one
sentence, a reader could not tell a weak signal from an ordinary position in
the multiframe.

### Against the acceptance criteria

- [x] **A view model checkable with `-lm` alone** -- 41 checks, two plain
      structs in, no `struct app`.
- [x] **`make check` and `tests/pipelines.sh` unchanged** -- 22383 checks,
      84 suites.
- [x] **The view model carries no raylib type.**
- [~] **`make screens NAMES="gsm"` unchanged.** Not established by bytes,
      and the reason is measured rather than assumed: **two renders from the
      same binary differ**, because the screen has a waterfall whose content
      depends on how many blocks the fixed duration caught. A byte comparison
      there proves nothing in either direction. What does: no file under
      `src/gui/` changed at all in the first commit, and after the window was
      pointed at the model the rendered screen was read and carries the same
      SCH line, the same BCCH line and the same sixteen neighbours.

### Three things GSM turned up

**The gate caught the omission this class of bug is named for.**
`check-viewer-link` keeps its own list of stream names -- *twice*, in two
tests -- which is not duplication but the second statement that makes the
first a check. Adding `gsm_state` to the enum and to `stream_names[]` left
both failing by name until the lists were updated. Ticket 07's own history
records the same shape slipping through twice when there was no such check.

**A fixed-height chart overflowed the viewport.** The channel-power canvas
started as `flex:0 0 120px` beside a waterfall and two readout lines, and the
panel scrolled by 121px at 1400x900 -- caught by `check-web-layout`, which is
the only thing that can see it. Both charts yield now, the waterfall weighted
twice.

**A stream reading `0/0` in the footer was a screenshot artefact, not a
publisher.** `link_health` arrives about once a second and its counts are per
connection, so a tab selected two seconds before the capture shows its new
stream at zero while the data renders above it. The server's own disconnect
tally -- `gsm_state sent 115` against 119 received by a client -- is what
settled it. Recorded in the skill so the next view does not spend the same
hour.

And one thing about the tooling: `scripts/web_layout.mjs` takes `--file`,
`--rate`, `--freq` and `--extra` now, because looking at a GSM view over an
FM capture shows a correct layout full of "awaiting" -- a picture that proves
the layout and nothing else. Its server duration also had to allow for the FM
tab's 30-second wait for the station name, which over any other capture is
spent in full every run; the old budget had the server exiting before the
last tab was reached.
