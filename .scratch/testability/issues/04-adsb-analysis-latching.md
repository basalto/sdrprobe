# 04 — Which frame the ADS-B analysis charts are describing

Status: ready-for-agent
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
