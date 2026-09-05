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

## Comments

**2026-09-05 — attempted, measured, reverted. The trough is not the answer,
and the reason narrows the ticket usefully.**

Two changes, built and measured on air, then backed out.

**Smoothing the topographic gate.** `survey_carrier_smoothed()` takes the
strongest bin within 10 kHz before the *grouping* judges anything, which is why
a multiplex groups into one carrier there; `sdr_dsp_find_peaks()` judged raw
bins and never did. Adding the same smoothing to `topographic_prominence()`
left a 470-690 MHz sweep unchanged at 36 candidates and 36 carriers, and merged
the ARFCN 69 capture's six maxima into three. Neutral to slightly good.

**Measuring the floor outward from the hump**, on top of that. It recovered the
1090 MHz carrier in `adsb_cpr_pair.bin`. It did not recover ARFCN 63 -- the
smoothing costs that one, because 10 kHz of it raises the saddle inside a
200 kHz GSM carrier's skirt and drops its topographic prominence under the bar.
And UHF went from 43 candidates to 53:

| | candidates | added | inside a DVB-T channel |
| --- | --- | --- | --- |
| committed | 43 | -- | -- |
| ADR-0017's first attempt | 71 | 28 | 28 of 28 |
| with the gate smoothed | 53 | 17 | **17 of 17** |

Smoothing removed eleven of the twenty-eight and no more.

## What that rules out, and what it points at

Prominence cannot separate the two cases, by any walk. A ripple on a multiplex
and an isolated weak carrier are the same shape: a local maximum with a real
descent either side and no -20 dB point within reach. The trough walk does not
help, because the ratchet that makes `survey_carrier_edge()` right for *extent*
-- keep descending, break only on a 6 dB climb -- never terminates in flat
noise, so an isolated tone's walk runs to the array's end exactly as the -20 dB
walk does.

What does separate them is not the candidate at all. It is that **a multiplex
ripple falls inside a carrier that the survey is already reporting**, and an
isolated tone does not. The measurement is already there: `survey_carriers_from()`
groups every UHF multiplex into one carrier of 3-6 MHz at -38 to -53 dBFS, and
each of the 17 additions above sits inside one of them, 20-odd dB down.

So the next attempt should leave `sdr_dsp_find_peaks()` permissive and put the
judgement where the context exists -- in what gets *reported*. A candidate
inside a stronger carrier's extent is a feature of that carrier;
`survey_carrier_holding()` already answers "which carrier holds this
frequency", and `struct survey_candidate` would gain the carrier it belongs to
rather than the peak finder guessing without it. That also fixes the thing this
ticket cannot: the survey would say *why* a maximum was set aside, instead of
silently not finding it.

Not started. The ordering matters -- candidates are currently computed before
carriers in all three call sites -- and that is a real piece of work, not a
patch.
