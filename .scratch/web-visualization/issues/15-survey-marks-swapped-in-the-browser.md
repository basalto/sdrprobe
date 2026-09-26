# 15 - The browser draws two of the survey's four marks swapped

Status: ready-for-agent
Found 2026-09-26, while reviewing layer boundaries (`.scratch/layer-boundaries/`).

## The fault

`survey_state` sends each candidate's mark as the integer value of
`enum sdrgui_peak_mark` (`sdrgui.h`):

```
SDRGUI_PEAK_SIGNAL = 0     filled dot   nothing known against it
SDRGUI_PEAK_RECEIVER = 1   cross        on the receiver's own comb
SDRGUI_PEAK_EMPTY = 2      hollow dot   a closer look found nothing
SDRGUI_PEAK_CONTESTED = 3  cross + dot  on the comb and reads displaced
```

`web/views/survey.js` decodes it with its own table, in a different order:

```js
// 0 signal, 1 empty, 2 receiver-like, 3 contested (sdrgui.h).
const MARK_CLASS = ['mark-signal', 'mark-empty', 'mark-receiver', 'mark-contested'];
const MARK_GLYPH = ['●', '○', '✕', '✕'];
... ['#5adcc8', '#8291a0', '#ff9b64', '#ff6864'][c.mark]
```

So in the browser **a candidate on the receiver's own comb is drawn as a
hollow grey "nothing here" dot, and a candidate a confirmation pass found
empty is drawn as an orange receiver cross** -- in both the table and the
chart. The comment even cites `sdrgui.h` as its source.

This is the worst pair to swap. `CLAUDE.md` gives the precedence and the
reason for it: *"empty wins over receiver-like, because 'there is nothing
here' is what a reader acts on"*. The browser tells a reader to stop
looking at spurs and to keep looking at empty frequencies.

## Why nothing caught it

Shipped with ticket 07 and green ever since. The wire carries a bare
ordinal, `check-viewer-link` asserts only that `"mark":` is present, the
page harnesses assert DOM structure, and no capture survey produces all four
marks at once. A second reader re-declaring an enum's order is exactly the
"two presentations kept in step" the view-model seam exists to prevent --
kept in step by nothing.

## Fix

**Send the mark by name**, the way the same message already sends `shape`
(`survey_shape_name()`): `"mark":"signal"|"receiver"|"empty"|"contested"`.
A named value cannot be mis-ordered, and a name the browser does not know is
visible rather than silently becoming a different mark.

- `viewer_link_publish_survey_state()`: a name function beside
  `sdrgui_survey_peak_mark()` -- or, better, in the model layer, which is
  `.scratch/layer-boundaries/issues/03-*`'s job; either works for this fix.
- `web/views/survey.js`: key `MARK_CLASS`, `MARK_GLYPH` and the chart
  colours by name.
- `check-viewer-link`: assert a candidate built to be receiver-like arrives
  as `"mark":"receiver"`, and one built empty as `"mark":"empty"` -- the
  pair that was swapped.
- `scripts/viewer_client.py` if it prints marks.

## Acceptance criteria

- [ ] The wire names marks; no reader indexes a table by a mark ordinal.
- [ ] A check pins receiver-like and empty candidates to their names.
- [ ] A browser drawing of a survey containing both kinds shows the cross on
      the comb candidate and the hollow dot on the empty one -- looked at,
      not only asserted (`node scripts/web_layout.mjs --png`, on a live
      sweep, since a capture survey has no comb and no confirmation pass).

## The same hazard elsewhere on the wire

Every other enum crossing the socket is also an ordinal that a view
re-declares: `fm_state.reading_tone` (`views/fm.js`'s `TONE_COLOR` --
correct today, by care rather than construction), `survey_state`'s
candidate `seen`, and `receiver_state`'s `tab` and `decode`, which each view
matches with a literal (`tab: 2, decode: 0`). None is known wrong. All are
the same shape as this one; `.scratch/layer-boundaries/issues/03-*` makes
"enums cross the wire by name" a contract rule.
