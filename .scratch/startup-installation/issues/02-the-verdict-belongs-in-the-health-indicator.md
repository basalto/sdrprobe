# 02 - The verdict belongs in the health indicator, not in a popup

Status: resolved, 2026-09-12

The earlier draft of this ticket built a notice that dismissed itself after
ten seconds. That was wrong, and the reason is worth keeping: **a calibration
verdict is a standing fact about the receiver, not an event.** Ten seconds
later the crystal is still corrected, still by that amount, still against that
reference -- and a surface that erases itself is a surface that cannot answer
"what am I corrected by?" at any later moment. The header already has the
right widget for a standing fact, and it has had it since ADR-0006.

No `notice.h`, no `sdrgui_notice()`, nothing self-dismissing anywhere.

## What is already there

`draw_health_indicator()` draws two dots, `chrome_layout.h` places them, and
`sdrgui_health_dot()` renders one from plain data (ADR-0007). Each already
carries a state, a label, a channel name and a channel number:

| dot | state today |
| --- | --- |
| GSM cal | `cal.drift_health` -- grey, green, amber while re-checking, red on drift |
| LTE cal | grey, amber while measuring, green once `lte_valid` |

So the startup calibration's verdict has a home already: a GSM lock turns the
GSM dot green with its ARFCN, an LTE lock turns the LTE dot green with its
EARFCN, and a failure leaves the relevant dot grey -- which is the honest
answer, and the one ADR-0006's own comment insists on ("claiming a health it
has not checked would be worse than an honest grey").

## The three faults the audit found in it

**1. A green dot says nothing.** `sdrgui_health_dot()` draws its `notice` only
in the CHECKING and DRIFT states. A receiver that calibrated successfully
shows a green circle and no number anywhere on screen -- not the correction,
not the reference, not when it was measured. That is exactly the question this
feature is meant to answer at a glance.

The fix is **on hover, not as a banner**: a permanent line under the header
saying "+32 ppm from ARFCN 113" would be on every screen for the rest of the
session, and the program would have bought clutter to avoid a popup. Hover is
the surface for detail on demand. It needs the applied ppm, the source
(`CALIBRATION_SOURCE_FCCH` / `_LTE` / `_CENTROID`), the channel, and how long
ago -- which the machine in ticket 01 already has.

**2. The banner's position is hardcoded inside the widget, at `22, 178`.** The
`centre` field exists precisely because "two widgets each deciding where they
sit is two widgets that can land on each other", and two lines below that
comment both dots write their banner to the same literal coordinates. They
cannot currently be in a banner state at once -- GSM CHECKING requires
`!cal.open` and LTE CHECKING requires `cal.open` -- but that is an accident of
two unrelated conditions, not a rule, and the startup machine adds a path that
runs GSM then LTE within one sequence. Move both to `chrome_layout.h` where
`check-layout` can see them.

**3. `cal.drift_notice` is written and then drawn nowhere the operator is
looking.** A drift re-check that completes on the Survey tab leaves its answer
in a string that only the header's red state renders. The dot itself does turn
red from any tab, so this is a smaller gap than the earlier draft claimed --
but the *reason* is invisible until the operator hovers, which is what fault 1
fixes for all four states at once.

## What to build

- `sdrgui_health_params` gains what a hover needs: applied ppm, source,
  measured-at. Plain data, no `struct app`.
- `sdrgui_health_dot()` draws a hover panel when the pointer is inside the
  dot. **Whether the pointer is inside a circle is geometry, so it goes in
  `sdrgui_geometry.h`** with the rest of the hit-testing, not as a distance
  test inside a draw call -- `check-geometry` already owns "which bar is under
  the pointer" and this is the same question.
- Both banner positions come from `chrome_layout.h`.
- The startup machine's verdict sets the dots on Continue, through the same
  fields the Apply PPM button already sets (ticket 04, step 3).

## Acceptance criteria

- `make check-layout` covers both banner rectangles and both dots, at
  1100x720 and 640x400.
- `make check-geometry` covers the dot hit test, including the pointer just
  outside the radius.
- A screenshot of each of the four states, and of the hover panel. A change
  that draws is not finished until somebody has looked at it.
- Nothing in the program dismisses itself on a timer.

## Not in scope

- A drift monitor behind the LTE dot. It still has none, and saying so stays
  more honest than inventing one.


## What was built

- `sdrgui_health_params` gains `banner`, `hover`, `hovered`, `have_detail`,
  `ppm`, `source` and `measured_ago`; `sdrgui_health_dot()` draws a hover
  panel and takes both positions from the layout.
- `chrome_layout.h` gains `gsm_banner`, `lte_banner` and `hover`. The two
  banners are stacked and `check-layout` asserts they do not overlap, are
  inside the window, and that the hover clears both chrome buttons -- which it
  did not at first: the hover was at y=88 and the Calibration button ends at
  92, so `check-layout` failed it at every size.
- `sdrgui_point_in_circle()` in `sdrgui_geometry.h`, with `check-geometry`
  covering the edge, just outside, and **the corner of the bounding box**,
  which is 1.41 r from the centre and must miss -- the reason this is not a
  rectangle test.

No `notice.h`, nothing self-dismissing. The reasoning is in ADR-0024.
