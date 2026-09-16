# 10 — A marker click decides inside the drawing, and nothing can reach it

Status: resolved, 2026-09-16
Blocked by: (none)
Opened 2026-09-15, from ticket 09's scope note.

Ticket 09 took the *message log's* row click out of the drawing: what a click
asks for is `srd_log_row_intent()`, and which row is under the pointer is
`sdrgui_message_log_row_at()`. It left the other half of the same fault
standing, deliberately and in writing rather than by ticking the criterion.

**Waterfall marker clicks still decide inside `sdrgui_waterfall()`.**
`sdrgui_scope.c:664` writes `*params->out_clicked_marker_id` from inside the
draw loop, and `draw_adsb()` and `draw_srd()` consume it to set
`selected_log` — two draw functions changing selection, which is the thing
ticket 09's acceptance criterion says must not happen:

```c
int clicked_marker = -1;
draw_waterfall_rect_with_markers(app, 0, l.waterfall, &app->srd.window,
                                 markers, marker_count, &clicked_marker, NULL);
if (clicked_marker >= 0)
    s->selected_log = clicked_marker;     /* view_srd.c:692 */
```

## Why it is worth doing, beyond the rule

The SRD waterfall's markers are the **only** way to select a burst by pointing
at where it was heard, and the mapping from a pointer to a marker is exactly
the arithmetic this repository has got wrong before and caught only by
clicking: `sdrgui.h` already records it for the survey chart —

> a marker drawn at a frequency the hit test maps somewhere else is the same
> bug the bar charts had, one or two positions out and only visible by
> clicking.

That inverse pair is checked for the survey chart (`sdrgui_survey_chart_x_at`
against `sdrgui_survey_chart_hz_at`). For waterfall markers there is no
forward function, no inverse, and no check. The markers also moved recently —
`340c613` fixed them to the tuning that heard them and `607212c` changed what
an unlabelled marker draws — so the geometry is live code, not settled code.

## What it would take, with the parts measured rather than guessed

The extraction looks harder than it is, and the two surprises are worth having
before starting.

**The axis is already available to a caller.** `sdrgui_waterfall_span()` is
public and returns the lower and upper frequency. `visible_seconds` is plain
arithmetic over `params->height`, `rows`, `pair_count`/`fallback_pairs` and
`sample_rate` (`sdrgui_scope.c:569`). Neither needs a window.

**The bracket rectangle needs no font.** Centre from frequency and age, width
from bandwidth with a 20 px floor, height from duration with an 8 px floor,
then clamped into the plot. That is `sdrgui_geometry.h`'s kind of arithmetic
today, unchanged.

**`resolve_label_pill()` is already pure.** It takes a target rect, the rects
already placed, the plot and the marker's centre, and returns a rect; its only
raylib call is `CheckCollisionRecs`, which is arithmetic. The part that looks
like the blocker — eight placement attempts against a 64-slot budget — can move
as it stands.

**The one thing that genuinely needs a window is the pill's width**,
`MeasureText(m->label, 12)`. That is the case `sdrgui_geometry.h` was built
for and already handles twice: the drawing measures and passes the answer in,
as `gutter` and as `struct sdrgui_log_widths`.

### The design question, and the answer

Pill placement is **order-dependent**: each pill is resolved against the ones
already placed, so marker N's rect depends on markers 0..N-1. A hit test that
re-derived placement independently would be a second implementation that
agrees only by luck — the same fault as the row band having its own arithmetic
in two places, which ticket 09 removed.

So the geometry function computes **every** marker's bracket and pill in one
pass and returns the array; the drawing and the hit test both read it. One
placement pass, one answer, and the hit test is a lookup rather than a
recomputation.

### One property to preserve deliberately

The current loop overwrites `out_clicked_marker_id` on every match, so with
overlapping markers the **last** match wins — and since drawing and hit-testing
share the loop, the last match is the marker drawn on top. That is the right
answer reached by accident. Whatever replaces it should return the topmost
match because that is correct, and a check should say so, rather than
reproducing an iteration order and hoping.

## Tasks

- [x] Add a marker-layout function to `sdrgui_geometry.h`: markers, the span,
      `visible_seconds`, the plot and the measured label widths in; every
      marker's bracket rect and resolved pill rect out.
- [x] Move `resolve_label_pill()` into it unchanged, and check its eight
      placement attempts and its clamping directly.
- [x] Add `sdrgui_waterfall_marker_at()` over that layout, returning the
      topmost marker under a point, or -1.
- [x] Make `sdrgui_waterfall()` draw from the layout rather than computing
      rects inline, so the two cannot disagree.
- [x] Remove `out_clicked_marker_id` from `struct sdrgui_waterfall_params`;
      keep `out_hovered_marker_id`, which reports rather than decides.
- [x] Move ADS-B and SRD marker selection into `handle_adsb_input()` and
      `handle_srd_input()`, beside the log-row selection ticket 09 put there.
- [x] Check the forward and inverse agree: a marker laid out at a frequency
      and age is found by a point at its own centre, for every marker in a
      populated waterfall.

## Acceptance criteria

- [x] No draw function changes selection anywhere in the program, which
      completes ticket 09's criterion rather than restating it.
- [x] `check-geometry` covers bracket placement, pill collision resolution,
      the plot clamping and the topmost-wins rule, with no window.
- [x] A mutation of the frequency-to-x mapping, of the age-to-y mapping, or of
      the topmost rule fails the suite. **State which mutations were run and
      what each produced** — ticket 09's first geometry check was phrased
      against its own answer and survived a mutation that dropped a whole
      heading block.
- [~] The ADS-B and SRD waterfalls render identically to before, compared as
      images. **Not available as stated, and the tick is a `~` for that
      reason**: the same binary renders the same capture differently on two
      runs, so there is no byte comparison to make. Established by eye against
      a before/after pair, and the control run first. See below.
- [~] Clicking a marker selects the same log entry it selects today.
      **A click cannot be injected here** -- the same reason `CLAUDE.md` gives
      for key presses -- so this is established by the forward/inverse check
      over a populated waterfall, by the topmost-wins check, and by the
      selection path being the same `selected_log` write moved from the draw
      to the input phase. Not by clicking.

## Not in scope

- Changing how a marker looks, where a pill goes, or the 64-pill budget. This
  moves the arithmetic and does not revise it; a rendering change mixed in
  would make the image comparison meaningless.
- The waterfall's drag-to-select band, which is a different gesture with its
  own path.
- Any further carve-out of `struct app`.

## Comments

Split out of `09-view-state-to-input-state.md` rather than folded into it. Nine
covered every message log — SRD, ADS-B and TETRA — and stopped where the work
stopped being the same shape: a log row is a band of constants, and a marker is
a two-axis projection with order-dependent label placement over it. Claiming
the "no draw function changes selection" criterion while markers still did
would have put a green tick over the half that was not modelled, which is the
fault `CLAUDE.md` names about layout headers holding half a screen.

## Done, 2026-09-16

`sdrgui_waterfall_marker_layout()` in `sdrgui_geometry.h` computes every
marker's bracket and pill in one pass; `sdrgui_waterfall_marker_at()` is the
inverse; `sdrgui_waterfall()` draws from the array and decides nothing.
`out_clicked_marker_id` is gone from `struct sdrgui_waterfall_params`, and
selection moved to `handle_marker_click()` in `view_srd.c` and `view_adsb.c`,
beside the log-row selection ticket 09 put there. No draw function in the
program writes a selection now.

**Two things the ticket did not anticipate, both forced.** `check-geometry`
links `-lm` alone, so `CheckCollisionRecs()` -- which is in raylib's *library*
-- could not come along; `sdrgui_rects_overlap()` reproduces it to the
character, strict inequalities included, because the placement was tuned
against those and a `<=` would move pills that currently sit edge to edge. And
`sdrgui.h` includes `sdrgui_geometry.h`, so `struct sdrgui_waterfall_marker`
moved down into the geometry header rather than the layout reaching up for it.

Three smaller ones worth recording. The marker construction itself had to come
out of both draw functions (`srd_markers_build()`, `adsb_markers_build()`),
because a hit test laid out against a *second* construction of the markers is
a second answer. The axes needed a shared derivation too --
`waterfall_marker_axes()` in `view_scope.c`, over `sdrgui_waterfall_span()`
and the new `sdrgui_waterfall_visible_seconds()` -- since the drawing derives
them inside `sdrgui_waterfall()` and the input phase cannot reach in. And the
leader line is drawn on a condition with **two** terms, one of them comparing
the pill against its pre-collision target, so the layout carries `pill_target`
rather than dropping it and quietly changing which markers grow a line.

### The mutations, and what each produced

`check-geometry` went from 190 to 230 checks. Run one at a time against the
finished suite:

| mutation | result |
| --- | --- |
| x mapping inverted (`upper - f`) | 1 failed |
| x mapping off by one bin (+2500 Hz) | 2 failed |
| y mapping inverted (`visible - age`) | 1 failed |
| topmost rule -> first match wins | 1 failed |
| bracket y-clamp removed | 1 failed |
| bandwidth floor 20 px -> 2 px | 1 failed |
| **pill x-clamp removed** | **passed** |

**The last one is the point.** `test_a_pill_is_clamped_into_the_plot` was
green and proved nothing: a single marker hard against the right edge never
reaches the clamp, because its pill is flipped to the *left* of the brackets
before placement and lands inside the plot on its own. What reaches the clamp
is a *collision*, which throws a pill to `marker_cx + 8` and past the edge --
so the check needs two markers at the same moment, not one. Rewritten that
way it catches the mutation, and the suite is 230 checks rather than 226.
That is the class `check-claims` calls "a green check can mean nothing at
all", found only because the ticket demanded the mutations be run and stated.

### The images, and why "identically" is not available

The criterion asks for the ADS-B and SRD waterfalls compared as images. They
are **not byte-comparable, and that is not a regression**: rendering the same
capture twice from the *same* binary produces different PNGs, because a
marker's age is `GetTime() - entry.at` and playback catches a different moment
each run. That control was run first, and it differs for both screens.

Compared by eye instead: ADS-B before and after are indistinguishable -- the
same twelve stacked `495211` pills in the same positions, only the log's wall
clock differs. SRD is structurally identical: same bracket-and-pill markers in
green, same stacking near 434.4 MHz, same leader lines, with the log holding a
different moment's frames. The pills stacked at the plot's left edge on ADS-B
are pre-existing and appear in both: a Mode S marker's bandwidth is 2 MHz
against a 2 MHz span, so its bracket is the whole plot and its pill is flipped
left and clamped.

`make check`: 21667 checks in 77 suites, no failures.
