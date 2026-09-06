# 01 - A candidate should carry the resolution it was found at

Status: resolved

The spec has the measurement. The question is what the survey should do about
it, and the answer is probably not "sweep finer" -- a full-tuner sweep at
3 kHz bins is not four minutes, it is hours.

## Where to start

The sweep already knows its own bin width; `survey_plan.bin_hz` is in every
report and in the saved JSON. What nothing downstream does is *use* it.

Two directions, both needing measurement first:

1. **Report it per candidate.** A candidate found in a 212 kHz bin and one
   found in a 3 kHz bin are different kinds of claim, and the report prints
   them identically. The narrowest thing a sweep can resolve is already
   computed -- `survey_tone_width_hz()` -- and a candidate whose measured
   width is at that floor is telling you only that something is there.
2. **Let the confirmation pass settle it.** The pass already revisits each
   candidate with six blocks at 2 MS/s, where a bin is 244 Hz. It has three
   orders of magnitude more resolution than the sweep that raised the
   candidate and currently answers only "present or not". Asking it for a
   width as well would turn every confirmed candidate into a resolved one,
   at no extra receiver time.

Direction 2 looks like the better value and it is the same move
`.scratch/receiver-comb/issues/02` wants for the comb flag, which is a reason
to do them together rather than a coincidence.

## What must be checkable

- Whatever is added is a pure function of a plan and a measurement, reachable
  from `check-survey-sweep` or `check-suspect` with no receiver (ADR-0012).
- The four fine sweeps in the spec are the regression: a report over the
  2026-09-05 full sweep must not present those sixteen land-mobile candidates
  as though they were resolved.

## What this must not become

A survey that refuses to report anything it cannot fully resolve. The
full-tuner sweep's job is to say where to look next, and it does that well for
the strong wide signals. The fault is the missing caveat, not the sweep.

## Answer

Half of it, by direction 2. The status stays open -- `ready-for-agent` rather
than `resolved` -- because the half that matters most to the baseline is the
half not done.

The `confirm` line now carries the width the closer look measured and the
suspicion flags at that resolution:

```
# confirm <frequency_hz> <claim> <verdict> <prominence_db> <hits>/<looks> <bandwidth_hz> <flags|->
confirm 230393982 new confirmed 47.3 6/6 2930  reference,unresolved
confirm 392127991 new confirmed 31.2 6/6 23438 -
```

A reader can now tell a 3 kHz tone from a 24 kHz channel among candidates the
sweep reported identically, because the pass measures at 244 Hz where the
sweep had 212 kHz. That is the whole of direction 2 and it cost nothing in
receiver time -- `sdr_carrier_report` already computed the width and the pass
already discarded it.

## What is left

Nothing. Direction 1 is done too -- see below.

### The other half, done

Direction 1 -- reporting the sweep's own resolution per candidate -- is **not**
done. A candidate nobody asked about still carries a width of zero and no way
to tell whether that means narrow or unmeasured, and the saved JSON still
records `width_hz: 0.0` for every entry of a full-tuner sweep.

That matters for the baseline, which is what this ticket was raised about: a
sweep saved without confirmation is still a list of 212 kHz maxima presented
as though they were signals. The cheap version is to record `plan.bin_hz`
alongside each candidate so a reader knows what the number could have meant;
the honest version is to run the confirmation pass before saving, which is
already possible and simply is not the default.


## Direction 1, done

The width was already there. `survey_candidates_from()` computes `extent_hz`
for every candidate from the peak's own bins -- `(upper - lower + 1) *
plan->bin_hz` -- and it was never reported. So a reader saw `width_hz: 0.0`
and could not tell an unmeasured candidate from a narrow one, when the sweep
had measured something for all of them all along.

The `candidate` line and the saved JSON now carry `extent_hz` and a
`resolved`/`floor` verdict, and the report says what the sweep could see:

```
old sweep   212.6 kHz to a bin; this sweep predates the extent being recorded,
            so how much of it was resolvable is unknown
new sweep    26.9 kHz to a bin, so 9 of 35 are at the resolution floor --
             their widths are lower bounds, not measurements
```

`survey_extent_is_floor(extent_hz, bin_hz)` is the predicate and
`check-suspect` covers it: a 25 kHz carrier is at the floor of a full-tuner
sweep's 212 kHz bin and resolved by a 27 MHz sweep's 3.3 kHz one, the *same
extent* answering differently because the bin it was measured in is different.
That is the whole point and it is why an extent cannot be read alone.

Two bins and a half is the boundary rather than one, because a maximum
straddling two bins occupies both, and calling that resolved would admit
exactly the narrowest things as though they had been measured.

### One thing to know about older files

They have no `extent_hz`, so the report says so rather than guessing. The
first version of that line printed "and every candidate is wider than it" for
a file with no extents recorded at all -- a claim about data the file does not
contain, which is the failure this whole ticket is about, committed by the fix
for it.
