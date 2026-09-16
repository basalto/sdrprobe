# The Settings panel throws the waterfall away

Every retune in this program now *shifts* the waterfall's history rather than
discarding it: a row is dBFS spread evenly across the received span, so moving
the centre changes which bin holds a frequency and nothing else, and
`sdr_dsp_retune_bin_shift()` / `sdr_dsp_shift_row()` slide the picture by
`(old - new) * bins / rate` with what slides in marked unmeasured
(`view_scope.c`).

One caller did not get that and still calls `recreate_waterfall(app, plot, 1)`
-- clear the history -- on every Apply: `apply_settings()` in
`overlay_settings.c`. It is the last one.
