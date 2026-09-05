# The candidate threshold is not the 8 dB it asks for, and is left that way

## Status

superseded by ADR-0017, which keeps this decision and corrects
the noise measurement it rests on

## Context

`sdr_dsp_find_peaks()` takes a `min_prominence_db`, the survey passes 8 dB
(`SURVEY_MIN_PROMINENCE_DB`), and the help overlay tells the operator that
candidates are "peaks standing at least 8 dB above the noise either side of
them". None of that is what happens.

Two separate things are going on, both measured rather than reasoned about:

**The gate and the report are different numbers.** The 8 dB is checked against
`topographic_prominence()` — how far you must descend before you can reach
higher ground. What is stored and displayed in the *above floor* column is
`power - local_floor`, a different quantity taken from a median of the band
either side. A candidate can pass the first and report a much smaller second.

**The effective threshold is nearer 20 dB, by accident.** After the gate, the
function walks out to the `bandwidth_db` point to size the candidate. A
candidate standing less than `bandwidth_db` above its own noise has no such
point: the walk runs to the ends of the array, the floor window is then eight
times a width that is the whole array, `local_floor()` is left nothing outside
the candidate to take a median of, and returns a NaN — on which the candidate
is discarded. So candidates below about 20 dB survive only when the noise
happens to be ragged enough to stop the walk early.

## Decision

Leave it. Bound the walk in `sdr_dsp_characterise_carrier()` only, where the
same fault had a different and unambiguous consequence, and do not bound it in
`sdr_dsp_find_peaks()`.

The reason is that 8 dB is below what noise reaches, so making it real makes
the survey worse. Peak-held noise, which is what a swept survey folds:

| blocks peak-held | candidates from pure noise | strongest prominence |
| --- | --- | --- |
| 1 | 64 (the cap) | 16.0 dB |
| 4 | 64 (the cap) | 10.6 dB |
| 16 | 12 | 8.3 dB |

And on a live sweep of 470-690 MHz at a 0.2 s dwell, bounding the walk took the
survey from 38 candidates to **74**, of which 28 reported under 10 dB above
their floor and 15 under 5 dB. The accident is doing the filtering that an
explicit threshold should be doing.

## Consequences

- The stated 8 dB is wrong in the help text and in the constant's name. It is
  documented here rather than corrected in place, because the honest number is
  not a constant: it depends on the dwell, and the table above shows it moving
  from 16 dB to 8 dB as the peak hold deepens.
- A survey's candidate list is therefore stable and useful, and its lower edge
  is undefined. An operator hunting something weak should narrow the range,
  which gives finer bins and a deeper hold, rather than trusting that 8 dB
  means 8 dB.
- Doing this properly means separating the two prominences, choosing a
  threshold from the dwell, and re-checking the candidate counts against a live
  sweep. That is a piece of work, not a patch, and it should start from the
  measurements above rather than from the constant.
- `sdr_dsp_characterise_carrier()` *is* bounded, because there the same
  unterminated walk made it refuse to measure a carrier that was plainly in the
  chart, reporting "nothing measurable at that frequency now". Measuring one
  selected candidate has nothing to do with how many candidates are found, so
  the fix carries no risk to the list.
