# 01 — Find a candidate's edges the way the carrier grouping already does

Status: needs-triage

## Where to start

Not with a threshold. Three of them have been tried and measured (ADR-0017) and
the failure is the same each time: a fixed number of decibels down cannot tell
a ripple inside a multiplex from an isolated carrier with little headroom,
because neither reaches it.

The trough walk can, and `survey_carrier_edge()` is it. The work is to make
`sdr_dsp_find_peaks()` use that walk to set `lower_index`/`upper_index`, so the
floor is measured against what is actually beside the candidate.

The layering is the obstacle and it is worth thinking about rather than working
around: `survey_carrier.h` is a header over `sdr_dsp.h`, so the walk cannot be
called from inside it. Either the walk moves down into `sdr_dsp.{c,h}` and
`survey_carrier.h` calls it there -- one implementation, which is the point --
or the peak finder grows a way to be told where a candidate ends.

## What must be checkable

- The three signals ADR-0017 lost, all of them real and all corroborated by
  something other than the survey: ARFCN 63 at 947.6347 MHz in
  `gsm_arfcn_69.bin` (named by the cell's own System Information 2), the
  1090 MHz carrier in `adsb_cpr_pair.bin` (six decoded frames from the same
  capture), and a bare tone at 102.4 MHz. `check-pipelines` currently asserts
  the *absence* of the first two, with the reason written beside them; those
  assertions are the ones to invert.
- The ripples that must not come back with them. A synthetic band of contiguous
  flat multiplexes with ripple on top is in `tests/sdr_dsp_test.c` already, and
  a live 470-690 MHz sweep must stay near 40 carriers rather than 60.
- That the floor bar becomes the thing that filters. The measure of success is
  that the lowest reported prominence sits at the bar instead of 10 dB above
  it, which is what ADR-0013 asked for in the first place.

## What this must not become

A second grouping. The carrier grouping already turns maxima into signals and
must keep doing that; this is about where one maximum's own skirt ends, which
is the input to a floor measurement rather than a replacement for grouping.
