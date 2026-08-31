# 03 — The survey chart component

Status: resolved
Blocked by: 01

`sdrgui_survey_chart()` in `src/sdrgui_scope.c` (it is a Scope-tab chart):
power against absolute frequency across the swept range, with candidates
marked and one of them selected.

```c
struct sdrgui_survey_params {
    Rectangle plot;
    const float *power_dbfs;    /* count bins spanning lower_hz..upper_hz */
    int count;
    float sentinel;             /* not-yet-swept bins are not drawn */
    double lower_hz, upper_hz;
    const struct sdr_peak *peaks;
    int peak_count;
    int selected;               /* index into peaks, -1 for none */
    int hover;                  /* likewise */
    int sweeping;               /* draw the progress edge */
    int swept_bins;             /* how far the sweep has got */
    const char *empty_notice;
};

void sdrgui_survey_chart(const struct sdrgui_survey_params *params);

/* The candidate under `point`, or -1. Ships with the chart, because the plot
   sits inside the rectangle by a gutter only the component knows -- see
   sdrgui_scan_chart_channel_at(), which exists because that was learned the
   hard way. */
int sdrgui_survey_chart_peak_at(Rectangle outer, const struct sdrgui_survey_params *params,
                                Vector2 point);
```

A frequency axis over three decades needs care: label in MHz with a major
division that adapts to the span, and let the hover readout carry the exact
figure. Mark candidates with a tick above the trace rather than by recolouring
bins, so a 200 kHz signal in a 1.7 GHz sweep is still visible when it is
narrower than a pixel.

## Comments

**The chart drew outside its own rectangle at first** -- the frequency labels
and the caption under the trace landed on the panel below it, which is the
exact contract sdrgui.h spends a paragraph on. `survey_chart_area()` now
reserves that strip as well as the caption strip above.
