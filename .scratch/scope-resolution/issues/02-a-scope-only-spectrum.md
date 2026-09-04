# 02 — A second spectrum, for the Scope alone

Status: resolved
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

## Comments

**2026-09-04** — Done, and not the way this ticket was written. The user's
suggestion replaced a second array with a simpler idea: there is one spectrum
and its size is a function of what is on screen, because the consumers only
read it while their own screen is up.

`input_scope_owns_spectrum` is that function, and the shape of it matters more
than the contents. It is asked **every block** rather than set when a screen
changes -- three bugs this week were a transition that stopped being taken
when the survey became a tab, and "put the size back on the way out" is that
mistake waiting to happen. A predicate over state that is already true has no
transition to miss.

The case a tab test alone would have missed, and which the ticket did not
mention: **calibration is an overlay, not a tab.** It sits over the Scope and
measures a centroid and an FCCH tone in that very array, and the GSM channel
scan sits inside it. Both are in the predicate and both are checked.

The property this ticket asked for, measured rather than argued: with the
Scope at 8192, the survey's candidate lines hash identically to the 2048 run
and `gsm_arfcn_69.bin` decodes the same 31 SCH. `check-input` walks every
screen and asserts the Scope never owns the spectrum while a consumer could
run.

Changing the size clears the peak hold and the waterfall's history, because
both were gathered against a different number of bins and mean nothing under
a new one. The arrays are sized to the maximum and `spectrum_bins` says how
many are filled -- everything that reads them uses that, not the array's
length, which is a capacity.

`--fft points` sets it. Verified: "256 x 512-pair windows, bin 3906.250 Hz"
against "8 x 16384-pair windows, bin 122.070 Hz", which is the resolution
against averaging trade the spec predicted, on screen and in those words.
