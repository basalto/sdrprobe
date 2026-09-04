# 02 — A second spectrum, for the Scope alone

Status: needs-triage
Blocked by: 01

`spectrum_average[]` is shared with the survey, both band scans and
calibration, whose thresholds were chosen against 977 Hz bins. So the Scope
gets its own array at its own size, and the shared one keeps 2048.

## The work

A second buffer and a second call in `process_block`, **skipped entirely when
the chosen size is 2048** -- which is the default, so nobody who does not use
this pays for it. When it is skipped the Scope reads the shared array, as now.

The spectrum and the waterfall both read it. The waterfall's texture is built
per row from the spectrum, so its row width follows the size and the texture
has to be rebuilt when the size changes -- the same path a resize already
takes, which is worth reusing rather than rediscovering.

## What must be checkable

That the consumers are untouched, which is a property rather than a value: the
survey's bin width, the GSM scan's channel powers and the FM scan's floor must
be identical at every Scope size. A check that fixes the Scope at 8192 and
asserts the survey's numbers are bit-identical to the 2048 run is the one that
would catch this being wired to the wrong array.
