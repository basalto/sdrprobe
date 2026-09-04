# 01 — Candidates above 1550 MHz that no re-sweep can find

Status: ready-for-agent

Find out what produces them, and stop the survey reporting them as signals.

## Where to start

Not in the band plan. The label is a lookup and it is behaving correctly; the
fault is upstream, in what the sweep measured.

Three things to separate, cheapest first:

1. **The tuner near the end of its range.** The R820T is quoted to about
   1766 MHz and the survey sweeps to exactly that. Whether its gain, PLL or
   image rejection degrades over the last hundred megahertz is measurable:
   sweep 1500-1620 and 1650-1766 with a long dwell, several times, and see
   whether the candidates move.
2. **The sweep's own step boundaries.** A candidate that only appears at
   certain step edges is a fold artefact rather than a signal. The step plan
   is `survey_sweep.h` and `check-survey-sweep` covers its arithmetic; what is
   not covered is what happens to a *measurement* that straddles two steps.
3. **The negative prominence.** 1603.219 MHz reported -3.0 dB, a maximum below
   its own floor. Whatever grouping or floor estimate allows that is reachable
   without a receiver -- `survey_carrier.h` is a pure header with a check
   suite already.

## Done when

- The cause is named, with the evidence.
- **Prominence cannot come out negative**, and a check says so. That one is
  worth doing regardless of what causes the rest: it is a pure function of a
  peak and a floor, and it produced an impossible number.
- A candidate that a confirmation pass cannot find is either not reported or
  reported as unconfirmed. `--survey-confirm` already revisits each claim with
  six blocks and prints a verdict; the sweep's saved JSON does not record
  whether that happened.

## What this must not become

A level threshold. These are not weak -- one of them is 37 dB above its floor,
which is why they are worth chasing. Filtering by level would hide them and
leave the cause in place, and would also discard the genuinely weak signals a
long dwell exists to find.
