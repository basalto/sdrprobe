# 10 — A marker click decides inside the drawing, and nothing can reach it

Status: ready-for-agent
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

- [ ] Add a marker-layout function to `sdrgui_geometry.h`: markers, the span,
      `visible_seconds`, the plot and the measured label widths in; every
      marker's bracket rect and resolved pill rect out.
- [ ] Move `resolve_label_pill()` into it unchanged, and check its eight
      placement attempts and its clamping directly.
- [ ] Add `sdrgui_waterfall_marker_at()` over that layout, returning the
      topmost marker under a point, or -1.
- [ ] Make `sdrgui_waterfall()` draw from the layout rather than computing
      rects inline, so the two cannot disagree.
- [ ] Remove `out_clicked_marker_id` from `struct sdrgui_waterfall_params`;
      keep `out_hovered_marker_id`, which reports rather than decides.
- [ ] Move ADS-B and SRD marker selection into `handle_adsb_input()` and
      `handle_srd_input()`, beside the log-row selection ticket 09 put there.
- [ ] Check the forward and inverse agree: a marker laid out at a frequency
      and age is found by a point at its own centre, for every marker in a
      populated waterfall.

## Acceptance criteria

- [ ] No draw function changes selection anywhere in the program, which
      completes ticket 09's criterion rather than restating it.
- [ ] `check-geometry` covers bracket placement, pill collision resolution,
      the plot clamping and the topmost-wins rule, with no window.
- [ ] A mutation of the frequency-to-x mapping, of the age-to-y mapping, or of
      the topmost rule fails the suite. **State which mutations were run and
      what each produced** — ticket 09's first geometry check was phrased
      against its own answer and survived a mutation that dropped a whole
      heading block.
- [ ] The ADS-B and SRD waterfalls render identically to before, compared as
      images.
- [ ] Clicking a marker selects the same log entry it selects today.

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
