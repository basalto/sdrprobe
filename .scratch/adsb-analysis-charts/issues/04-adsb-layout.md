# 04 — adsb_layout.h, pinned by check-layout

Status: resolved
Blocked by: (none)

`src/view_adsb.c` derives its rectangles inline (`adsb_retune_button()`,
`adsb_log_rect()`). The analysis mode adds five panels that share rows, which is
exactly the drift `gsm_layout.h` exists to prevent.

Add `src/adsb_layout.h` as a pure function of the window size, in the shape of
`gsm_layout.h`:

```c
struct adsb_layout {
    Rectangle retune_button;
    Rectangle view_toggle;      /* View: Log / Analysis */
    Rectangle hold_button;      /* Hold last good */
    Rectangle chart[3];         /* upper row, packed by rectangle alone */
    Rectangle log;              /* full width in log mode, lower-left in analysis */
    Rectangle scatter;          /* square, lower-right, analysis mode only */
};

static inline struct adsb_layout adsb_layout_for(float width, float height);
```

Cover it in `tests/layout_test.c` at the same window sizes the other two
layouts are pinned at, including the 540 px minimum height — `gsm_layout.h`'s
comment records that panels there push their own buttons off the panel, so
check the same clamp here rather than rediscovering it.

The charts reserve their own caption strip and gutter (`sdrgui_chart_area`), so
pack them by rectangle and leave gaps only for looks.

## Comments
