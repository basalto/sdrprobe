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
2. **The sweep's own step boundaries -- now with evidence, and it is the
   likeliest of the three.** The receiver retunes in 1.6 MHz steps, and
   candidates cluster on multiples of exactly that:

   | multiple of | on it | by chance |
   | --- | --- | --- |
   | 1.5 MHz | 2.7% | 2.0% |
   | **1.6 MHz** | **6.7%** | 1.9% |
   | 1.7 MHz | 2.0% | 1.8% |
   | 1.8 MHz | 0.4% | 1.7% |

   The obvious confound is that 1.6 MHz is sixteen times the 100 kHz raster
   broadcast services sit on, so real stations would land there anyway. Two
   controls say otherwise. Restricted to **200-300 MHz, where no broadcast
   raster applies**, 1.6 MHz gives 8.3% of 48 candidates against 0.0% at
   1.5 MHz. And the 100 kHz raster itself shows **no enrichment at all** --
   31.4% against 30.0% by chance -- so the rasters are not what is doing this.

   A live sweep of 240-270 MHz makes it plain. Eight candidates, and every
   one is a multiple of 1.6 MHz to within 10 kHz: 240.009, 243.210, 244.810,
   246.407, 251.208, 259.210, 267.211, 268.801. That is the whole of the
   "Fixed and mobile" cluster in that range, which the full survey reports as
   51 candidates -- its second largest group.

   `survey_sweep.h` holds the step plan and `check-survey-sweep` covers its
   arithmetic. What is not covered is what happens to a *measurement* that
   straddles two steps, or to the bins at a step's edge where the tuner's
   response is rolling off.
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
