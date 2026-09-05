# Band surveys

One JSON per sweep, kept so they can be compared. The data lives in
`surveys/`, which is **gitignored**: a sweep measures one location with one
antenna, the same kind of thing as `captures/`, and it is nobody else's
baseline. This file is the format; the sweeps are yours.

A single survey says what is transmitting; a series says what changed, which is
the more useful question and the only one that needs the files to accumulate.

## From the survey window

The survey is where the application opens, and the **Survey** button at the
left of the Scope tab returns to it. It is a button rather than a fifth
numbered view because it is not a fifth way of looking at the current tuning:
the other four draw whatever the receiver is pointed at, and this one walks the
receiver across a band.

The view carries the same thing as the command line: a **site** and an
**antenna** field on the row under the range, and a **Save survey** button. The
fields start from the saved configuration and write straight back to it, so
naming the site here is the same act as `--site` and lasts as long.

Save refuses while the site is empty and puts the cursor in the field, rather
than writing a sweep labelled nothing. It writes the same JSON the script
ingests, so a sweep saved from the window and one piped from a headless run are
interchangeable, and one reporting tool reads both.

The site and the antenna are both **combos**: type a new one, or pick one this
receiver has used before. Picking beats retyping, because spelling one place --
or one antenna -- two ways makes it two of them, and nothing downstream can
tell. The antenna defaults to `telescopic`, which is what a dongle ships with,
and that name is in the list from the start so the picker is never empty.

**The tuning correction is kept per site**, and shown beside each name in the
list. It belongs to the receiver rather than the room, but it is measured
against whatever reference the room offers and it drifts -- so calibrating at
one place and carrying the dongle to another arrives with a number that was
true somewhere else. Picking a site applies the correction recorded there, and
`Apply PPM` in the calibration overlay records a new one against wherever you
are. `--ppm` still wins for one run, and is recorded against the current site.

## Maxima and signals

A peak finder returns local maxima and a transmission has more than one, so the
raw candidate list counts shoulders as stations. Every sweep is therefore also
grouped into **carriers**, and everything above the measurement -- what is new,
what is missing, what to ask again about, what to remember -- works on those.

Two maxima are one signal when nothing separates them: the power between them
never drops far below the lower of the two. A trough is what a band edge looks
like. Each carrier's extent runs out to the trough on either side, which is
also what makes a *weak* peak's width mean anything -- marking where it falls
20 dB below itself does not, since one already near the floor never gets there
and its extent runs to the end of the band.

A carrier reports two centres. `centre_hz` is the middle of its extent and is
what identifies it; `power_centre_hz` is where the energy actually sits. They
differ for a lopsided signal: a loaded LTE downlink measured on air had its
power a megahertz above its middle, and a history matching on that would call
the same carrier new every sweep.

The candidate list shows each maximum with the **width** and **shape** of the
signal it belongs to, and what this site has heard of it: `new`, `steady`,
`on/off` for something that comes and goes, `by hour` when the coming and going
follows the clock, or `gone`. The shape is a
description of the measurement -- `tone`, `narrow`, `medium`, `wide`, `very
wide` -- and never an identification; read it against the allocation, which is
the other half. Medium inside the FM allocation is a station; medium inside a
gap in the table is worth a closer look.

`report` says how many signals a sweep's candidates grouped into, and the
headless sweep prints a `carrier` line per signal alongside the `candidate`
lines -- the candidates are what was measured, the carriers what it was
concluded to mean.

## Watching

**Watch** sweeps, folds what it found into what the site knows, says what
changed, and sweeps again. From a script, `--survey-watch <n>` does the same
for a fixed number of sweeps and prints a line each:

```
watch sweep 3 carriers 10 appeared 0 quiet 1
watch-summary sweeps 6 appeared 16 quiet 13
```

The folding is the point. A watch that only looked would learn nothing --
every sweep would call the same signals new, because "new" means this site has
not heard it and nothing would ever have been written down.

It needs a site, and refuses without one: there would be nowhere to put what it
learns.

**It is also the only way a daily pattern becomes visible.** The history keeps,
per signal, how many of the site's sweeps in each hour of the day heard it --
against how many sweeps that hour has had. Counting sweeps alone cannot tell
"alternates minute to minute" from "runs from six until midnight", because both
are heard in half of them. So a signal whose presence follows the clock is
reported as **`by hour`** rather than `on/off`, which is the difference between
a transmitter worth investigating and an office that closes.

That classification is demanding on purpose: four distinct hours each swept at
least three times, and sixty percentage points between the busiest hour and the
quietest. Intermittent is the honest answer while the pattern is unknown, and
a daily rhythm claimed from two sightings would be reading tea leaves.

## Asking again about what changed

A sweep step is about a tenth of a second: enough to notice a carrier, not
enough to be sure of one. So the marks below are claims, and over three hundred
steps some will be wrong -- a transmitter between bursts reads as missing, a
moment of noise reads as new.

**Ask again** revisits each of them. It tunes to the frequency, takes six
looks and **peak-holds them into one spectrum**, and sees. A handful of targets
takes seconds against a sweep that ran for minutes, which is the same bargain
the LTE band scan's confirmation pass makes and for the same reason: a wide
search has to be generous, so something narrower must have the last word.

The hold is what makes six looks worth more than one. Averaging or overwriting
them would answer about the last block, and the signals this pass exists to
settle are largely bursty -- so the one block the transmitter was up in is
exactly the one that must not be thrown away.

What it finds goes into the history, rather than what the sweep guessed. A
"new" signal that does not hold up is never remembered -- once it is in, the
next sweep calls it missing and the noise becomes a permanent ghost.

From a script, `--survey-confirm` runs it as soon as a sweep finishes, and
prints the verdicts:

```
confirm 94728027 new refuted 2.9
confirm 95706500 missing refuted 47.8
confirm 96900000 missing confirmed 3.1
confirm-summary asked 11 confirmed 9 refuted 2
```

The middle line is why the pass exists: the sweep called that frequency missing,
and a proper look found it 47.8 dB above the floor.

**The window and a script ask about different things, deliberately.** The
window has a site history to lean on, so it revisits only what changed -- what
this site has never heard, and what it has heard and did not this time. A
headless sweep may be the first this site has ever taken, and its output *is*
the report, so `--headless --survey ... --survey-confirm` revisits **every
signal the sweep found**, strongest first, up to twenty-four.

That is what tells a standing transmitter from a moment of noise, and above
1.5 GHz the difference is most of the list. One 1400-1766 MHz sweep found ten
signals and the pass confirmed **one**; the same flag over band II confirmed
all twenty-four broadcast stations. Refuted entries are still reported and
still written down -- the verdict goes beside the signal, never in place of it
(ADR-0015).

## What the site remembers

Saving a sweep also folds it into `surveys/history-<site>.txt`, a small
line-oriented summary of everything that site has ever heard: each frequency,
the level it was last heard at, how many sweeps it has appeared in, and which
sweep it last appeared in. The JSON files are the archive; this is what the
window reads, and it is deliberately simple enough to edit or delete by hand --
delete it and it rebuilds from the next sweep you save.

With a history the survey window annotates the sweep in front of you:

- a **green tick** under a candidate this site has never heard before;
- a **hollow mark** where something it has heard is absent this time, drawn
  only inside the range the sweep actually covered -- a sweep of one band says
  nothing about a signal three hundred megahertz away;
- a **popup** under the cursor giving the frequency, the level, the allocation,
  and what the site remembers: *new here*, or *heard in 3 of 5 sweeps*, or
  *last 2 sweeps ago at -41.3 dBFS*.

Both marks are drawn under the trace rather than over it. A survey is a
measurement and the memory is an interpretation of it, and the two should not
be easy to confuse.

**Matching happens at the coarser of the two resolutions.** A sweep of the
whole tuner places a station to within 200 kHz; a sweep of one band places it
to 2. Comparing the fine sweep against the coarse memory at the fine tolerance
calls the same station new *and* the old entry missing at once, which is
exactly what the first live run did. Each entry therefore records the bin width
that placed it, and the better placement wins when they merge.

## Recording one from a script

```sh
./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
    --survey-save
```

`--survey-save` does what the window's Save button does -- writes the JSON and
folds the sweep into what the site has heard -- and prints where it went:

```
survey-saved surveys/2026-09-03-165948-88M-108M.json candidates 30 carriers 29
survey-history site home-sala-estar sweeps 2 signals 32 new 6 quiet 3
```

It refuses without a site, for the same reason the button does. Piping through
`ingest` still works and is the way to attach a `--note`.

## Recording one by hand

```sh
./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
    | ./scripts/survey_tool.py ingest --note "where the antenna was, and why"
```

The note is the part a later reader cannot recover. Levels are only comparable
between sweeps taken the same way, so record the antenna, the gain if it was
set, and anything unusual about the location -- a survey with no note is a
number nobody can use as a baseline.

24-1766 MHz is what an R820T reaches, and at 0.12 s a step it takes about four
minutes.

Files are named for the second they were taken, `2026-09-03-051319-24M-1766M
.json`, and neither writer will ever put one over another: if the name is
taken it picks the next free one. Sweeps are minutes of somebody's time and
there is no getting one back. The date alone was not enough -- four full
sweeps in one day once left a single file.

## Reading them

```sh
./scripts/survey_tool.py report surveys/<one>.json
./scripts/survey_tool.py diff  surveys/<older>.json surveys/<newer>.json
```

`report` groups by the band plan's own name for each frequency, which is what
turns a list of 247 candidates into a finding. `diff` compares only where the
two sweeps overlap, and says what appeared, what went, and what moved by 8 dB
or more.

**`diff` refuses two sweeps from different sites.** They are not a before and
an after; comparing them reports the move as though the band had changed, which
is the one way this archive can mislead rather than merely disappoint. Pass
`--force` if you have a reason. It also warns when two sweeps share a site
label but their fingerprints do not -- one of them was probably taken somewhere
else -- and when the antenna differs.

The `rf-environment` skill in `.claude/skills/` is the analysis layer over
these: what is known, what is new, and what is worth measuring next.

## What is in a file

`schema`, `recorded_at`, `range_hz`, `sweep` (steps, bins, dwell, blocks,
settling), `receiver` (tuner, antenna, gain_db), `site` (label, fingerprint),
`totals`, `confirmation` (asked, confirmed, refuted), `candidates` -- each with
`hz`, `dbfs`, `prominence_db`, `centre_hz`, `width_hz`, `flags`, `confirmed`
and `allocation` -- and `carriers`, each with `centre_hz`, `power_centre_hz`,
`lower_hz`, `upper_hz`, `width_hz`, `dbfs`, `prominence_db`, `maxima`,
`confirmed` and `allocation`.

Two conventions worth knowing before comparing anything:

- **Flagged candidates are kept, never dropped.** A `flags` entry means the
  survey thinks the candidate resembles the receiver rather than the band -- a
  reference comb, a DC offset at a step centre. The comb is spaced **1.6 MHz**,
  which is 28.8/18: every ninth tone is also a multiple of 14.4 MHz, which is
  all the survey used to know about. Two things have to agree before the finer
  comb is claimed, because 1.6 MHz is sixteen times the 100 kHz broadcast
  raster and one FM channel in sixteen sits on it: the sweep must be able to
  place the candidate to a fortieth of the spacing, which a sweep wider than
  about 650 MHz cannot, and the candidate must be a bare tone at that sweep's
  own resolution. 94.4 MHz, the loudest station at this site, is on the comb
  and 27 survey bins wide, and is not flagged. The survey does not remove
  them and neither does this, because removing a peak would hide a real
  transmitter that happens to sit on a harmonic (ADR-0015). `report` and `diff`
  set them aside and count them; they stay in the file.
- **`confirmed` says whether anybody went back and looked**, and its three
  values are three different facts: `confirmed` means a closer look agreed,
  `refuted` means it did not, and `unconfirmed` means no pass asked about this
  frequency. Writing the last two the same way is what leaves a reader unable
  to tell a signal that failed a second look from one nobody checked. A
  candidate takes the verdict of the carrier it is a maximum of, since that is
  what the pass tuned to.
- **`settling` counts the blocks a sweep threw away** after each retune,
  because they were captured while the tuner was still moving. It is not
  waste: folding them writes the previous step's signal into this step's bins.
  At a 0.10 s settle and a 0.10 s dwell it is about a third of what arrives.
- **`allocation` is a lookup, not an identification.** It says which
  allocation the frequency falls in, and nothing about what is actually
  transmitting there. Band 28 in the 2026-09-02 sweep is labelled "LTE band 28
  downlink" and is carrying 5G NR.

## Compare like with like

`diff` warns when two sweeps used a different dwell or gain, because it cannot
tell sensitivity from change. A longer dwell finds weaker peaks, so diffing a
0.12 s sweep against a 0.25 s one over the same band reports most of the
difference between the *sweeps* as though it were a difference in the air. Use
the same parameters for anything meant as a baseline.

## Absence is weak evidence

A 0.12 s dwell catches a bursty transmitter about as often as it misses it, so
one sweep finding nothing at a frequency means little. Two sweeps agreeing
means considerably more. This is the same rule the LTE band scan had to learn
the hard way -- see `LTE_SCAN_MIN_LOOKS` in `src/lte_scan.h`.
