# 09 — What a view says about itself, and what the input state believes

Status: resolved, 2026-09-15
Blocked by: (none)

Ticket 07 made the input *precedence* reachable: `input_route.h` holds a plain
`struct input_state`, and `check-input` walks every target and predicate over
it. What it did not make reachable is the step before — **how that struct gets
filled in** — and that step is where the decisions about individual views live.

`input_state_now()` in `src/sdrprobe.c` composes one field from five view
predicates:

```c
state.text_focus = survey_editing(app) || fm_editing(app) ||
                   srd_editing(app) ||
                   app->sv.field_focus != SCOPE_FIELD_NONE;
```

Each of those reads `struct app` and lives in a `view_*.c` that links raylib,
so no check reaches any of them, and none reaches the composition. The whole of
`input_state_now()` is in the same position: thirteen fields, each a small
decision about what a view is currently doing.

## Why it matters, with the fault it has already produced

`CLAUDE.md` describes this failure in its own words, having paid for it by
bisection: `GetCharPressed()` drains a queue, so whoever reads it first takes
every character that frame. `chart_key_pressed()` empties it in a `while` loop
and the frame loop calls it gated on `input_takes_typing()`. A field that its
view does not report is a field whose characters are gone before its handler
runs — and the symptom is **not** a wrong answer but silence, reported as "the
receiver label does not accept text input".

That has now happened twice. The settings panel's PPM field and the startup
form's fields were each swallowed this way (fixed in `25e0144`), and the gate
they had to reach was widened in response — but nothing stops the next one.
The SRD view gained a second typing field on 2026-09-15 (`srd_editing()`
returns `typing || freq_typing`) and the only thing that makes it correct is
that somebody remembered.

`check-input` cannot see it: it takes `text_focus` as an *input*. Green suite,
dead keyboard.

## What to build

Make the input state a checked projection of plain per-view state. Each view
reports the few input facts it owns without reading `struct app` or raylib;
`input_state_now()` folds those facts into `struct input_state` and contains no
view-specific decision of its own.

Move the SRD log-row action out of `draw_log()`. Drawing reports which row was
clicked; the input path decides whether that row selects an entry and requests
a retune. Fixing a historical log entry's absolute frequency must likewise be
expressed by a plain operation over the tuning that heard it, rather than by a
writer that reads the application's current receiver state.

Do not add an input registry. It replaces one enrolment obligation with
another and adds a seam with one adapter; it does not make the module deeper.

## Why this is deep

The interface is the view's input facts and actions, not its entire state.
Deleting the module would put typing ownership, input precedence inputs,
row-selection rules and retune intent back into the frame loop and draw
functions. The module therefore provides leverage to every view and locality
for the rules that decide who receives a key or click.

## Implementation plan

1. Inventory the fields of `struct input_state` and name which view or overlay
   owns each fact.
2. Add pure per-view projections over the view's own state for facts that are
   currently computed in raylib-linked files.
3. Fold those projections in one presentation-free input-state module and
   make the frame loop an adapter over its result.
4. Make SDR log-row selection return an action or intent; handle selection and
   retuning during the input phase, never while drawing.
5. Form an SRD log entry from an event plus the tuning that heard it, so its
   absolute frequency is fixed by a checked operation.
6. Extend `check-input` with each typing surface and SRD row action, then run
   the assembled input and pipeline checks.

## Tasks

- [ ] Assign every `struct input_state` fact to one owning view, overlay or
  application rule.
- [ ] Add pure input-state projections for Survey, Scope, FM, SRD, Settings
  and Startup typing state.
- [ ] Make `input_state_now()` a fold with no direct view-field decisions.
- [ ] Move SRD log selection and retune out of `draw_log()` and remove its
  const cast.
- [ ] Construct `srd_log_entry.absolute_hz` from the tuning that heard the
  session event through a plain checked operation.
- [ ] Check every typing surface, including both SRD fields.
- [ ] Check valid, invalid and capture-mode SRD row actions.
- [ ] Keep input precedence and existing keyboard behavior unchanged.

## Acceptance criteria

- [ ] `check-input` fails when any typing surface is omitted from the input
  state projection.
- [ ] No draw function changes selection, retunes a receiver or casts away
  `const struct app *` to perform an action.
- [ ] Clicking an SRD log row selects it and retunes a live receiver to the
  fixed frequency that heard it; capture playback never requests a retune.
- [ ] Retuning after an SRD event cannot move that event's recorded absolute
  frequency.
- [ ] Per-view projections take plain view state and require no window,
  receiver or `struct app`.
- [ ] Existing input precedence and pipeline checks pass unchanged.

## Adjacent, and smaller

Two things found the same day and worth folding in rather than filing
separately:

- **`draw_log()` in `view_srd.c` retunes the receiver.** Clicking a row tunes
  to that row's frequency, which is a decision taken inside a function that is
  `const struct app *` and casts the const away to act. It works, and it is
  the only cast of its kind in the file, but a view that decides should not be
  doing it from inside a draw — `CLAUDE.md`'s rule is that a function which
  draws may not also decide. The decision is one line (`which row, therefore
  which frequency`) and belongs beside the other input handling.
- **A log entry's frequency is fixed when it is written**, and nothing checks
  it. `srd_log_entry.absolute_hz` exists because the waterfall used to place
  markers at `applied.frequency_hz + carrier_hz` every frame — with the
  *current* tuning — so retuning dragged every historical label along with it.
  An offset only means something beside the tuning it was measured against,
  and once the receiver can be moved from that screen it does not stay beside
  it. The fix is right by construction and unreachable by any check, because
  the writers take `struct app`.

Both are instances of the same thing this ticket is about: the view layer holds
decisions that its own types could express and its own checks could reach.

## Blocked by

None - can start immediately.

## Not in scope

- Replacing `input_route.h` or changing input precedence.
- A registry of typing flags or callbacks.
- A uniform interface for all views.
- Moving raylib event collection into presentation-free modules.
- Refactoring unrelated draw functions or receiver-retune paths.

## Comments

Promoted to `ready-for-agent` after the 2026-09-15 architecture review. The
review selected the plain per-view projection and rejected the registry as a
hypothetical seam. Report: `/tmp/architecture-review-20260915-175714.html`.

**Done 2026-09-15.** `src/view_input.h` is the module: `struct view_input` is
the plain facts, `view_input_state()` is the fold, and `input_state_now()` in
`sdrprobe.c` is thirty lines of field copying with nothing left to decide.
`src/srd_log.h` is the SRD half. 198 checks in `check-input`, 189 in
`check-geometry`, all 68 suites green (20980 checks). v0.58.1 -- PATCH, since
no command line, headless report or file format moved.

**A shipped bug was found by doing it, and it is the ticket's own failure
class.** `struct waterfall_signal_context` -- the right-click menu and the
retrospective signal report from `.scratch/iq-ring-buffer/issues/03-*` --
reached `struct input_state` nowhere; `sdrprobe.c` did not contain the string
`wf_menu`. `q` is tested *before* `handle_waterfall_context_input()`, so
**pressing `q` while the signal report was open quit the program**, and Escape
on the context menu quit rather than closing it. Two surfaces were added to a
view and the routing was never told, which is this ticket in one sentence.
`input_state.report_open` is the fix: it suppresses the shortcuts the way Help
does, and its own handler keeps Escape.

## What the module actually buys, and what it does not

The enum is the mechanism. `enum typing_surface` names all seven, and
`view_input_set_typing()` is the one place a surface is mapped back to the
field carrying it -- so `check-input` sweeps `TYPING_SURFACE_COUNT` rather than
the five somebody thought of. Three mutations were run and all three go red:

| mutation | result |
| --- | --- |
| the SRD frequency field dropped from the fold | 5 FAILED |
| a surface added to the enum and wired nowhere | 3 FAILED |
| `report_open` no longer suppressing the shortcuts | 2 FAILED |

**What it cannot see, and the ticket said so before it was built**: a new
typing field inside a `view_*.c` whose own predicate does not report it. That
obligation moved; it did not vanish. This is the honest version of the claim
the registry was rejected for making.

## Two things that came out of it and were not planned

**`survey_editing()` and `srd_editing()` are deleted.** They existed to answer
the frame loop's one question, the frame loop now asks `view_input.h`, and a
predicate with no caller is the deletion test answering itself. `fm_editing()`
survives because the FM view asks it of itself. A first draft kept all three
behind a comment saying they "stay as the views' public answers" -- which was
false when written, and is the shape of wrong claim beside right arithmetic
that `check-claims` is about.

**The message log's row band moved into `sdrgui_geometry.h`.** Reaching the
decision from the input phase means finding the row without drawing it, and
the hover highlight and the hit test now share one answer where each had its
own arithmetic. `sdrgui_message_log_params.out_clicked_row` is gone: reporting
a click out of a draw is what let the SRD view retune from inside one.

**The first version of that geometry check was worthless and said so under
mutation.** Every assertion was phrased against `band.first_y`, so dropping
the heading block moved the expectations along with the answer and the suite
stayed green -- a round trip in the small, exactly what `CLAUDE.md` warns
cannot check a convention both sides share. It is anchored on the four
constants the drawing used as literals before the extraction (25 px caption,
12 pad, 22 headings, 24 row), and all four mutations now fail.

## Scope, honestly

Done: all eight tasks. ADS-B and TETRA were folded in beyond the ticket --
both also set `selected_log` from inside their draws, without a const cast --
so all three message logs decide in their input phase now.

**Not done, and not claimed** -- now `10-a-marker-click-decides-inside-the-drawing.md`:
waterfall *marker* clicks still set selection inside `draw_adsb()` and
`draw_srd()` (`clicked_marker`). The acceptance
criterion "no draw function changes selection" is therefore met for message
logs and not for markers. Extracting that means lifting marker geometry out of
`sdrgui_waterfall()`, where position depends on the chart window and each
marker's age -- a bigger job than this ticket asked for, and it wants its own
ticket rather than a silent tick here.

The three screens whose logs changed were rendered and compared: `adsb`, `srd`
and `tetra` draw identically, no column shift and no row displacement.
