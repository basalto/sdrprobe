# 07 — The measurements behind the numbers

Status: resolved
Blocked by: (none)

The LTE view reported a correlation and a margin and nothing else, which is
the same position the GSM view was in before its burst charts: a number cannot
say whether it was a clean lock or a coin toss.

`struct lte_trace` in `lte_dsp.h`, filled only when a caller passes one, the
way `gsm_sch_symbols` is. View: Charts draws four things.

| Chart | The question it answers |
| --- | --- |
| PSS correlation, 96 samples either side of the peak | Was it a sharp lock, or a broad hump -- a reflection, or nothing? |
| SSS candidate scores, all 168 | By how much did the winner beat the field? This is the gate the whole search turns on. |
| Channel across the broadcast's 72 subcarriers | Why did the broadcast not decode? A notch here is a reason, and it is invisible everywhere else. |
| Broadcast elements | Four corners is a message. A cloud is not. |

The correlation profile is a second, tiny pass over 193 alignments after the
peak is found, rather than a buffer carried through the 28800 the search
actually runs. The trace is only collected while the charts are up.

The second chart is the one that would have saved the most time: the
conjugated primary sequence (`issues/04`) produced a sharp PSS peak and a
candidate chart that was flat noise, which is a picture of exactly what was
wrong.
