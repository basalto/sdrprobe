# 04 — survey_layout.h, pinned by check-layout

Status: resolved
Blocked by: (none)

`src/survey_layout.h`, in the shape of `adsb_layout.h`: a pure function of the
window size.

```c
struct survey_layout {
    Rectangle from_field, to_field;   /* range entry */
    Rectangle sweep_button;
    Rectangle stop_button;
    Rectangle chart;                  /* power across the swept range */
    Rectangle peak_list;              /* lower left */
    Rectangle detail;                 /* lower right */
    Rectangle inspect_button;         /* inside detail, when a decoder fits */
    float header_left, header_right;
};
```

Pin every rectangle at 1100x720, 1280x800 and the 1000x540 minimum, and assert
the properties the numbers exist to protect: the two lower panels share a top
and bottom edge, the inspect button stays inside the detail panel, and the
header text stops before the buttons on its row.

## Comments
