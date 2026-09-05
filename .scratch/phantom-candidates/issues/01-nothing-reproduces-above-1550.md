# 01 — Candidates above 1550 MHz that no re-sweep can find

Status: resolved

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

## Comments

**2026-09-05 — resolved. There were three faults, and the one this ticket
named was not among them.**

### The 1.6 MHz clustering is the receiver's own comb, not the sweep's steps

The evidence in the ticket is real and its reading was wrong. Sweeping the same
air three times with the step grid moved underneath it settles it: the range's
lower edge sets where the steps fall, so `240M:270M`, `239.2M:270M` and
`239.5M:270M` walk three different grids over the same band.

The candidates did not move. All three sweeps put them at 240.008, 243.210,
244.811, 246.408, 251.211, 259.209, 267.210 and 268.799 MHz -- on step
*boundaries* in the first, on step *centres* in the second and on neither in
the third. A frequency that ignores the grid is not made by the grid.

What it is locked to is the receiver's clock. With `--ppm 0` the tones land on
exact multiples of 1.6 MHz -- offsets of +2.5, -0.8, -0.5, +0.6, +1.3, +2.3,
-0.4, +0.3 and +0.7 kHz against a 3.7 kHz bin -- and with this site's +35 ppm
correction applied they all read about +35 ppm high. A transmitter moves the
other way when the correction changes; a spur divided down from the same
crystal that clocks the tuner and the ADC keeps its ratio to the nominal grid,
which is exactly what these do.

And 1.6 MHz is 28.8/18, so every ninth tone is also a multiple of 14.4 MHz --
the comb `survey_suspect.h` already knows, established by the unplug test.
In the `--ppm 0` sweep, 11 of 14 candidates sit on the 1.6 MHz comb and the
existing detector flagged the two that happen to be on the coarser one
(244.8 = 17 x 14.4, 259.2 = 18 x 14.4). The confound the ticket anticipated is
the reason this is not simply fixed here: 1.6 MHz is sixteen times the 100 kHz
broadcast raster, so flagging on it would mark 94.4 MHz -- the loudest FM
station at this site -- as the receiver, and flagged candidates are dropped
from the report's per-allocation bests. Raised as `.scratch/receiver-comb/`.

### Above 1550 MHz the signals are real, and bursty

Five identical sweeps of 1550-1766 MHz, minutes apart, same antenna and gain:

| sweep | candidates | where |
| --- | --- | --- |
| 1 | 0 | -- |
| 2 | 6 | 1612.2, 1623.6, 1624.5, 1633.1, 1636.2, 1649.1 |
| 3 | 6 | 1614.2, 1622.2, 1624.9, 1636.6, 1644.4, 1646.0 |
| 4 | 2 | 1612.2, 1632.5 |
| 5 | 2 | 1613.2, 1629.8 |

Fifteen frequencies, **no frequency twice**, at 30 dB and more above their
floors. That is 1610-1660 MHz -- Iridium and the mobile-satellite uplinks --
which is short bursts on channels that move. The spec's re-sweep finding
"nothing at all" was the first row of this table, not a refutation.

So the sweep was not inventing them and a confirmation pass is the right
instrument, which is what "Done when" asked for. It was not reachable:

- `--headless --survey --survey-confirm` was accepted and did nothing. The
  headless sweep has its own loop and never ran the pass. It now does.
- In the window the pass was triggered from inside `survey_find_peaks()`,
  which runs on **every block of every dwell** -- so it fired on the first
  block of the first step with seven bins of eight thousand measured, found
  nothing new, and cleared the flag for good. Moved to the end of the sweep.
- Six looks were folded into `spectrum_average`, which every block rebuilds
  from scratch, so "six blocks folded into one spectrum" was the sixth block
  alone -- the wrong instrument for a bursty signal by construction. The looks
  are peak-held now, in one place both paths use.

With that working the three cases separate cleanly, in one run each:

| band | signals | confirmed | refuted |
| --- | --- | --- | --- |
| 88-108 MHz (broadcast) | 24 | 24 | 0 |
| 240-270 MHz (the comb) | 14 | 8 | 6 |
| 1400-1766 MHz (satellite) | 10 | 1 | 9 |

The eight that hold up at 240-270 are the comb tones; a crystal spur is
genuinely there every time you look, which is why the verdict is not the same
question as the flag.

### The negative prominence

Reproducible and impossible, and worth the ticket on its own. It came back at
1603.24 MHz reading -3.4 dB in a 600-1766 MHz sweep two days after the -3.0 dB
at 1603.219 the spec recorded.

`local_floor()` leaves out the peak's own hump and nothing else, so inside a
busy stretch it measures the *neighbours*. A maximum sitting in a notch between
two carriers passes the topographic gate -- there is a real descent either side
of it -- and its -20 dB width walk then stops inside the notch, so the hump left
out is one bin wide and the median comes off the carriers, which are above it.
`tests/sdr_dsp_test.c` builds that shape and gets -12.7 dB from it.

A maximum below its own floor is now dropped. Not clamped to zero, which would
publish a number by hiding it, and not a level threshold: it says nothing about
how strong a peak must be, only that it must be above its own surroundings. It
removed one candidate of 289 in the sweep that started this.

### One more thing, found on the way

The headless sweep folded every block it consumed, including the ones that
arrived during the settle. `survey_sweep.h` says in as many words that those
hold the previous step's samples and folding them "reads as a real carrier at a
frequency nothing is transmitting on"; the window has always obeyed it and this
path never did. At a 0.10 s settle and a 0.10 s dwell that was a third of
everything measured -- `survey blocks 270 settling 135` on a 135-step sweep.

Whether it was *producing* ghosts is a separate question and the answer looks
like no: an 88-130 MHz sweep with the fault present shows no candidate at any
strong station's frequency plus 1.6 MHz, so the pipeline latency is inside the
settle. It is fixed because the two paths must agree and because the header
says so, not because it explains anything above.
