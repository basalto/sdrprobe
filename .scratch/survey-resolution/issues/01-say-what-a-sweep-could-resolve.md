# 01 - A candidate should carry the resolution it was found at

Status: ready-for-agent

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
