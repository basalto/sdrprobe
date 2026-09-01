# 04 — Which frame the ADS-B analysis charts are describing

Status: resolved
Blocked by: (none)

`update_adsb` in `src/view_adsb.c` decides which frame trace the analysis
charts show: the newest, or the one the operator latched. The charts have no
caption that proves which, so a latch that silently follows the newest frame is
invisible — the charts keep looking plausible while describing something else.
`adsb_log_push` and the funnel counters (seen, preamble, CRC-passed, decoded)
are the same kind of decision, and the funnel is the operator's evidence about
whether the antenna or the DSP is the problem.

What it would take: `adsb_trace_select(latched, newest, have_new)` returning
which trace to show, plus the funnel as a plain accumulate-and-report struct.
Check: no frames yet, a latch that must hold across new arrivals, a latch on a
frame that has aged out of the log, and funnel counters that can only decrease
down the chain.

## Comments

## Answer

Done in `src/adsb_analysis.h`, checked by `tests/adsb_analysis_test.c`
(`make check-adsb-analysis`, 80 checks).

`adsb_receiver_ready()`, `adsb_analysis_visible()`, `adsb_trace_keep()`,
`adsb_trace_shown()`, `adsb_log_push/fade()`, `adsb_totals_add()` and
`adsb_funnel_is_consistent()`. `struct adsb_log_entry` and
`ADSB_LOG_CAPACITY` moved out of `app.h`, which now includes the header.

Writing the checks corrected an assumption worth recording: Hold pins the last
frame that *passed* its CRC, and a newer passing frame does replace it. What it
keeps out is the flood of failed attempts between good frames -- which is the
whole reason it exists, since most attempts fail and the charts would otherwise
flick to noise exactly when someone wants to read one. The check now pins that
distinction: twenty failed attempts must not get through, a good one must.

Also checked: a block with no preamble leaves both traces alone rather than
blanking the charts; holding before anything has passed shows the latest
attempt rather than an empty trace; the log is newest-first and drops the
oldest at capacity; and the funnel must narrow -- attempts within preambles,
failures and decodes within attempts, and no frame counted as both. Each
inconsistent shape has an operator reading it as evidence about their antenna.
