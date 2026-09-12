# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` carries the exhaustive per-file and per-view tour (every CLI flag, key,
button, and file). This file covers the build/test loop, the big-picture
architecture, and the invariants that are easy to break. Read `AGENTS.md` when you
need the detail.

## Build & test

```sh
make                  # build ./sdrprobe (needs librtlsdr + raylib dev headers, pkg-config)
make check            # everything below, ~57 s, no window and no receiver
make check-touched    # only the suites covering what git says changed
make check-dsp        # the four DSP checks below
make check-sdr-dsp    # one check in isolation — generic core
make check-gsm-dsp    # GSM module (+ the core it reuses)
make check-adsb-dsp   # Mode S / ADS-B module
make check-band-plan  # the frequency allocation table
make check-options    # the command line: every flag, value, and rejection
make check-survey     # the survey window's zoom, pan and clamp arithmetic
make check-survey-sweep # the sweep's step plan, fold, and measurement
make check-survey-session # the survey's machine: sweep, ask again, watch, measure
make check-suspect    # candidates that look like the receiver, not the band
make check-reading-origin # whose oscillator a reading belongs to
make check-clock-chain # a clock family in octaves, not harmonics
make check-lte-chain-analysis # one LTE chain walk, over both LTE captures
make check-calibration # the lock gate, and the machine that fills its buffer
make check-scan       # the band scan's coverage and the channel it chooses
make check-adsb-analysis # trace latching, the message log, the funnel
make check-gsm-continuity # whether consecutive SCH decodes hang together
make check-gsm-bcch   # four bursts to a System Information message
make check-lte-transport # CRC-24A, the fillers, and the circular buffer
make check-tetra-dsp  # a TETRA carrier to dibits
make check-tetra-sync # descramble, depuncture, Viterbi, and the parity
make check-acquisition # the block slot, both its modes, and its shutdown
make check-sample-format # the same signal in an 8- and a 16-bit container
make check-device-profile # what a receiver is, in the terms the numbers need
make check-capture-sidecar # what a capture says about its own bytes
make check-device-backend # the contract a receiver has to satisfy
make check-installation # what a measurement belongs to
make check-add-argument # the refactoring tool below, against its own traps
make check-gsm-session  # a GSM decode, block by block, no window
make check-tetra-session # a TETRA decode, block by block
make check-lte-session  # an LTE decode, and the repeat a message needs
make check-adsb-session # Mode S, and the even/odd pairing across blocks
make check-fm-session   # an RDS decode, and the bits it must not recount
make check-layout     # GSM view geometry (raylib headers only, no window)
make check-geometry   # where a chart's plot sits, and which bar is under the pointer
make check-input      # which control a key press reaches
make check-pipelines  # the built program over testfiles/, asserting on stdout
make hooks            # run `make check` on every git push (once, per clone)
make clean
```

**Run the suite that covers the change, not all of them.** Most suites are
under a second and the full set is **about three minutes**, so `make check`
after every edit turns a fast loop into a slow one.

**Where that time goes, and what took it from 242 s to 57.** Four things,
each measured, none of them a guess:

1. **The units run in parallel** -- `-j$(CHECK_JOBS)` with
   `--output-sync=target`, which buffers each suite's output so the report
   still reads as a report. 242 to 171.
2. **`signal_find_carrier()` was fixed**, which is where 54 s of one suite
   went. Its coarse grid was four times the main lobe it probed with and had
   blind frequencies; correcting it made the scan both right and 3.5 times
   faster. 171 to 115. See `SIGNAL_COARSE_PAIRS`.
3. **`CHECK_UNITS` is ordered longest-first.** `make -j` starts targets in
   list order, so a long suite late in the list adds its tail to the end of
   the run instead of overlapping it. 115 to 95.
4. **`check-pipelines` is one more job in the pool**, started first, rather
   than a serial phase after the units. 95 to 72, and the work since has
   taken it to 57.

**`CHECK_JOBS` is half the cores, not all of them, and that is measured** --
these suites stream large float arrays and saturate memory bandwidth before
they run out of cores, so on this eight-core machine the units phase read
-j2 88 s, -j3 71, **-j4 66**, -j5 68, -j8 72, -j16 78. Past four, another job
makes every running job slower.

**What is left is a floor, not waste, and that is also measured.** The units
phase is 49 s and `check-signal-probe` alone is **45** -- so the other 57
suites and all ~200 translation units fit in four seconds of slack behind it.
`check-pipelines` is 35 s and entirely overlapped. Which is why
`.scratch/gate-time/` ticket 02 -- caching the compilation nothing caches --
is **wontfix**: a 58-rule Makefile rewrite to buy four seconds of
fifty-seven. It would be worth its day the moment the pole stops being one
process, and the ticket says what would do that.

The loop is three sizes:
`make check-<one>` while iterating, `make check-touched` before committing --
it reads each check rule's own prerequisites to pick, and prints how many
suites it skipped -- and `make check` as the gate, which `make hooks` makes git
run for you: it points `core.hooksPath` at `scripts/hooks/`, whose `pre-push`
refuses a push whose tree does not pass. `git push --no-verify` is the escape
hatch. Nobody should have to type `make check`; the hook is what remembers it,
and it is the only gate this repository has -- there is no CI behind it.

A picked run is worth what it says and no more: three of twenty-eight suites
green is three suites green. That is enough while working and is not a claim
that a change is sound. **ADR-0012
governs what belongs in it: every decision the program makes must be reachable
by a check that needs no window, no receiver, and no person — drawing is
exempt, deciding is not.** The rule that keeps that true is that a function
which draws or reads input may not also decide; if it computes a threshold,
chooses a range, maps a pointer to an index, or advances a state machine, that
part comes out into a unit with a name. Logic not yet reachable is listed in
`.scratch/testability/`, one ticket per area — add to it rather than leaving a
gap implicit.

**What the window was told, and what it showed** — `--debug-log FILE` (or `-`
for stderr) records one line per event: every key raylib received with the
handler the router sent it to and the screen it landed on, every click with
its coordinates, every retune, and the screen whenever it changes. Off by
default and free when off.

```sh
./sdrprobe --view fm --duration 20 --debug-log /tmp/run.log
```

It answers the question a report of "the key did nothing" cannot: whether the
program never received the key, received it and routed it elsewhere, or routed
it correctly to a handler with nothing bound. Those are three different bugs
that look identical from outside, and this repository has spent an hour
telling them apart by bisection. **Keys cannot be injected here** — `wtype`
synthesises a keysym on a scratch keycode while raylib reads physical ones, so
a requested `h` arrives as something else entirely, and `/dev/uinput` is
root-only. The log is what is left, which is why `check-debug-log` pins the
key names and the target names against `input_route.h`'s enum: a log that
mislabels what it saw turns an unanswered question into a wrong answer.

**A change that draws is not finished until somebody has looked at it.**
`make screens NAMES="gsm lte"` renders those screens from captures -- no
receiver, about six seconds each -- into `build/screens/`, and the Read tool
displays a PNG. Look at the ones your change touched, and at their
neighbours; bare `make screens` renders all twelve and takes a minute, which
is the wrong tool for a change that moved one panel.

`src/lte_findings.h` turns those numbers into sentences, in the broadcast
panel and as `lte-chain-finding` lines. One of them is a refusal and it is the point: a Doppler and a residual tuning
error are one phase, so at 1.16 km/h per hertz the drift measures the crystal
with any motion buried inside it, and indoor against outdoor is not measured
at all.

**The delay spread is bounded at both ends and the bounds are not the obvious
one.** An earlier version refused to place it among 36.104's profiles because
EPA (45 ns), EVA (357) and ETU (991) are all finer than the ~1010 ns of one
over the references' 990 kHz span. That was the wrong criterion: 1/span
resolves individual taps, and this estimator measures the *scatter* of the
phase steps, which for a spread tau is 2*pi*90kHz*tau. Its floor is the noise,
`1/sqrt(rho)` of phase error per step -- 559 ns at 10 dB of RS-SINR, 177 at 20,
70 at 28 -- so EVA and ETU are comfortably measurable at the 28 dB the cells
here read and only EPA is out of reach. The real limit is at the top: the
scatter follows sin(phi) rather than phi, so it reads 9% low by three quarters
of a radian (1326 ns) and the steps wrap at one (1768 ns), past which the
number is not a delay. All three cases are named on screen for what they are.

The cell panel is a table of what each measurement The cell panel is a table of what each measurement *did*, not what it says
this block: smallest, mean and largest since the identity last changed
(`src/lte_stats.h`, and `lte-chain-stat` lines in the headless report). Every
one of those moves -- a correlation drops when somebody walks past the
antenna, a reference power follows the fading -- and one reading cannot tell a
marginal cell from a steady one. **The reset on a change of identity is
load-bearing**: a carrier here alternates between two cells block to block,
and an average across both would sit under a heading naming one of them. It
also caught a fault nothing per-block could show: the chain was picking its
primary cell by reference power, an invented identity's power reads high often
enough to take first place, and the primary flipped often enough to reset a
run of 146 blocks to 3.

Panel rows are part of that geometry, and `src/panel_rows.h` owns it for every
view that has a table of fields. It gives a panel's row positions, its label
and value columns and how many rows it *holds*; a row past that capacity is
not drawn at all, because off the bottom edge is worse than absent, so a
caller orders its rows and the ones that fit are the ones that matter.

It is shared rather than copied for the reason `sdrgui_geometry.h` is shared,
and because four copies of a row step is how five panels end up with four row
heights. The spacing stays per-view -- eighteen-point network fields and
fifteen-point decode statistics do not want the same step -- so each layout
header names its own and passes it in.

The measurement behind it: the views computed rows as `y +=` between draw
calls, so `check-layout` saw only the rectangle and passed while the FM signal
panel drew **101 pixels past its bottom edge at 640x400** and TETRA's identity
panel 56. `check_panel_rows()` in the layout check now walks all of them, and
adding three to a capacity fails it sixty-three times.

`check-layout` is necessary and nowhere near sufficient. It compares
rectangles, so it cannot see two panels drawing into the *same* rectangle, a
picker offering bands the receiver cannot tune, a field reading "N/A" under a
caption that promises a number, a message naming the wrong technology, or a
frame that came out blank. Every one of those shipped in this program, and all
five were obvious in a screenshot. If a change adds geometry it goes in the
view's layout header -- **all of it, not some of it**: a header holding half a
screen puts a green tick over the half it does not model, which is worse than
having none.

**`make check` passing is not the same as being right.** A round trip cannot
check a convention both directions share, and this repository has lost months
to that twice — a conjugated LTE primary sequence and a scattered GSM SCH field
layout, both green throughout. Before trusting a decode, diagnosing one that is
wrong, or pinning a real-capture answer in a check, the `dsp-validation` skill
in `.claude/skills/` carries what corroborates such an answer and what merely
agrees with it. **Whether a change *improves* anything is a different question
with its own failures** -- measuring where the answer cannot show, comparing
two implementations at different gains, drawing noise once -- and
`does-it-help` carries those, along with how to choose a constant by measuring
where it breaks -- and **a refactor that is supposed to change nothing is the
same question run backwards**: the survey's machine came out of its view with
55 suites green, both capture surveys byte-identical and the screen
byte-identical, while the settle that throws away stale blocks was disabled.
A capture never retunes, so no capture can exercise it. What caught it was one
line the program already prints about itself, `survey blocks 26 settling 13`,
on a live sweep.

**A check that fails on its first run is more often a wrong claim than a found
bug.** These checks carry prose, and prose can be false beside impeccable
arithmetic: six wrong claims were written in one session -- a property
asserted of a superset, a worst case confused with a measurement, two things
asserted not to collide that cannot coexist, an expression subtracting a term
from itself. Read the claim before changing the code; the `check-claims` skill
carries how.

There is no CI and no linter, and the test framework is one header:
`tests/check.h` holds the counters, the comparisons (`check_int`, `check_close`,
`check_size`, `check_str`, `check_true`, and `check_msg(condition, fmt, ...)`
for a message of your own) and `check_report("what it covers")`, which prints
the suite's one-line summary and returns the exit code. A check is still a
`main()` calling `test_*()` functions; to run a *single* test, temporarily
comment out the others — there is no filter flag. `make check` hides the
compiler commands so it reads as a report; `make V=1 check` shows them, which
is what you want when a build fails rather than a check. `check-pipelines` is the exception: a POSIX
`sh` script (`tests/pipelines.sh`) that runs the built binary over the captures
in `testfiles/` and greps its stdout, which is what proves the units are wired
together.

**Threading a new parameter through a function with dozens of call sites** --
which this repository keeps needing, and which was a scratch script three times
before it became a tool:

```sh
make add-argument FILE=src/foo.c FUNC=bar INDEX=1 VALUE='&app->source'
make add-argument FILE=--self-test
```

`scripts/add_argument.py` inserts an argument at a position in every call to a
named function. **It is literal- and comment-aware, and the scratch version was
not** -- asked to insert at index 2 in `f(a, "comma, inside", b)` it produced
`f(a, "comma, NEW, inside", b)` silently, because a comma inside a string sits
at brace depth 0. It survived three real refactors only because every insertion
happened to be at index 0 or 1, ahead of any such string. `check-add-argument`
pins that case and nine others. It is a text tool and rewrites a prototype the
same way it rewrites a call, so read the diff.

White-box diagnostics (not tests — they print a walk through a decode chain and
compile the module's `.c` in to reach its statics):

```sh
make probe-gsm-chain                        # defaults to testfiles/gsm_arfcn_69.bin
make probe-gsm-chain FILE=captures/x.bin
make probe-adsb-chain FILE_ADSB=testfiles/adsb_modes1.bin
make probe-lte-chain FILE_LTE=testfiles/lte_b20_pci28.bin
make probe-periodicity FILE_PERIODICITY=captures/x.bin   # LTE or NR? which grid?
make probe-signal FILE_SIGNAL=captures/x.bin AT_SIGNAL=300000 \
    CONTROLS_SIGNAL=-200000,600000                      # on air, or noise?
make probe-fm-filter FILE_FM_FILTER=testfiles/fm_rds_tsf.bin  # RDS: which biphase filter?
make probe-two-cell                          # two cells on one carrier: how often?
```

`probe-periodicity` is the odd one out: it demodulates nothing, and works on a
signal no technology module here understands. Two lag correlations -- a burst folded over
its own period, and the cyclic prefix against itself -- say whether a carrier
is LTE (a burst every 5 ms) or 5G NR (every 20, and none at 5), and whether it
runs at 15 or 30 kHz. It is how band 28 was found to be carrying NR rather than
a weak LTE cell. Both measurements now live in `signal_probe` -- they were
statics in a `main()`, so nothing in the program could call either -- and this
file is the walk and the conclusion over them.

`probe-nbiot` is the gate the `rf-environment` skill demands before a
technology gets a ticket, written for NB-IoT and useful as a shape. It
correlates against the narrowband primary synchronisation signal -- a
length-11 Zadoff-Chu, the same in every cell, repeating every 10 ms -- and
reports the peak against its own floor and how much of it comes back a frame
later. **`FILE_NBIOT=--self-test` lays the sequence into noise and finds it at
12.8 deviations with a 105% repeat**, which is the half that makes a null
worth anything: a negative from a detector nobody has seen fire is not a
finding. Six band 8 carriers read 2.9 to 5.1 deviations and 50 to 79%, against
a known-empty LTE capture at 4.0 and 68%.

`probe-lte-chain` reports what the multi-cell search made of each block
beside what the single-cell one did, because until that line existed
`lte_cell_search_all` had **no route to a capture at all** -- `--lte-chain`
refuses a file, so it was reachable only with a receiver attached, which is
how it came to lose a cell on a committed test capture with nothing noticing.
Its summary says how often it came back silent and how often the single-cell
search refused, and the first may never exceed the second.

`probe-two-cell` is the odd one out in a different way: its subject is a
*check* rather than a capture. It builds the two-cell fixture over forty draws
of its interfering traffic and reports what the multi-cell search makes of
each, which is what turned "this check is flaky under clang" into a number --
and it is compiled from `tests/lte_dsp_test.c` itself, with `-DLTE_TWO_CELL_SWEEP`,
because a harness that copies a fixture is running a second fixture.
`MODE_TWO_CELL=--fixture` hashes the fixture stage by stage, which is how two
compilers can be asked where they stop agreeing.

`probe-signal` is what `signal_probe` says about a capture **at a signal and
at its controls**, and the shape is the point: a measurement at one frequency
is a number, and the same measurement where nothing should be is what makes it
evidence. An AIS null was worth nothing until the same code put a known TETRA
carrier 16 dB clear of its own controls. It exists because one session wrote
six variants of it -- for the symbol-rate line, the bursts, the envelope, the
spectral shape, the AIS channels and the ILS sidebands -- and threw every one
away with the answer left in a transcript.

`PAIRS_SIGNAL` limits how much of the capture is used, and **it used to change
the answer**: `carrier_power_fraction` mixes at one fixed frequency, so a
drifting carrier walked out of phase over a long look and the mean cancelled
against itself -- which reads exactly like modulation, because modulation is
what the statistic is looking for. The 75.0005 MHz harmonic read 0.888 to
0.921 from 0.07 s to 1 s and **0.779 at 2 s**, crossing
`SIGNAL_BARE_FRACTION` and turning a bare carrier into a modulated one.

It is a mean over **segments** of `SIGNAL_STANDING_SEGMENT_BLOCKS` now, and
the same capture reads 0.905 at one block and 0.923 at 2 s. The segment length
is measured from both ends -- one block reads exactly 1.0 whatever it holds,
so B blocks of noise read about 1/B, while a long segment cancels -- and 64
has the most drift margin of the lengths tried while keeping noise an order of
magnitude under the threshold: a bare carrier stays over 0.98 out to 10 Hz/s,
0.13 ppm per second at 75 MHz.

Two things about that fix are worth carrying. The per-segment fractions are
**averaged** rather than their numerators and denominators summed: the two are
identical to three decimals on anything power-stationary, and they part
company on a carrier that keys on and off, where averaging returns the duty --
which is what the unsegmented form gave -- and summing calls a carrier keyed a
tenth of the time "nearly bare" at 0.790. And **the threshold was re-derived
rather than kept**: every negative in the corpus rose (the highest is now
Mode S at 0.327 against FM's 0.145), 0.80 still sits 0.12 under the lowest
positive and 0.47 above the highest negative, and on the one block both
shipped callers hand it every verdict is unchanged.

`probe-tone` asks whether a clock-coherent tone is at one frequency, and whose
clock it is. It was a shell one-liner written about a dozen times in one
afternoon -- sweep a window, find the candidate, subtract -- and every number
of that campaign landed in a transcript rather than a ticket, which is what
`AGENTS.md` says a repeated scratch script is for.

```sh
make probe-tone FREQ_TONE=150M                    # crystal from the config
make probe-tone FREQ_TONE=480M APPLIED_TONE=0     # sweep uncorrected
```

**It reports the candidate nearest *each* hypothesis and not the one nearest
the nominal**, which is the trap it was written the wrong way round first: the
two predictions sit either side of the nominal and can be tens of kilohertz
apart, so nearest-to-nominal picks another signal whenever one is closer. Asked
about 480 MHz it reported a candidate 5.4 kHz below and called it unexplained,
while the coherent tone sat 15.1 kHz above. It is the same trap
`reading_origin.h` documents for channel rasters -- the hypothesis decides
where to look -- and it is worth knowing that it bit twice in one file.

`probe-survey-threshold` answers a different kind of question: what a survey of
*nothing* reports. Pure noise through the real transform and the real fold, at
every fold depth a sweep can have, and the answer is no candidates at any bar
down to zero -- which is what ADR-0017 rests on and what ADR-0013 assumed the
opposite of. It is also a warning about where that leads: knowing noise is not
the constraint is not the same as knowing what is, and ADR-0017 records three
replacements for the candidate threshold that were built, measured on air, and
put back.

```sh
make probe-survey-threshold                 # and DRAWS=12 for more of them
```

**A second receiver is coming, and the gate against it is a format the
program cannot yet read.** `.scratch/device-model/` is the spec; ticket 01 is
done. `scripts/rescale_capture.c`, behind `make rescale-capture`, writes an
8-bit capture into the 16-bit container a 12-bit device delivers, and
`check-sample-format` asserts the two arrive as **bit-identical** floats --
identical, not close, because `(byte - 127.5) * 16` is exact and 2040 is
exactly 127.5 times sixteen.

```sh
make check-sample-format                    # rebuilds build/testfiles16/ first
make rescale-capture FILE_RESCALE=captures/x.bin OUT_RESCALE=/tmp/x16.bin
```

That float comparison settles the whole program, which is why no capture is
decoded twice to establish it: **there is exactly one byte-to-float seam**,
`sdr_dsp_convert_iq()`, and everything downstream takes floats -- the
byte-taking `fm_discriminate()` survives with no caller outside tests,
`view_fm.c` having moved to `fm_discriminate_f()`. Identical floats means
identical answers by construction rather than by measurement. Both halves of
that comparison now go through the shipping converter rather than a harness.

**Full scale is the profile's, and it is nowhere else.** `grep -n '127\.5'
src/` returns comments and `device_profile.h`'s own two format functions.
`sdr_dsp_convert_iq()` takes a profile and reads the container, the full scale
and the bytes per pair from it; clipping, headroom and the transform's scale
read the same number, `lte_reference_power()` takes it for the two dBFS
readings that need it, and the scatter view normalises by it.
`PHYSICAL_MAGNITUDE_MAX` -- 127.5 root two, an 8-bit number that lived beside
a block size in `acquisition.h` -- is now `device_magnitude_max()`, a fact
about the container. **The floats stay in the device's own counts** and are
deliberately not normalised, because clipping means "at the ADC's rail" and a
rail is a count: a 12-bit part clips at 2047.5, nowhere near its container's
32767.5. `SPECTRUM_TOP_DBFS` stays a display constant, and is dB relative to
whatever the profile says full scale is -- which is what it always meant.

**Full scale of that corpus is 2040.0 and not a 12-bit part's 2047.5**, and
the ticket asked for 2047.5. It is the ordinary shape of a wrong claim beside
right arithmetic: 2047.5 is correct for an AD9361 and wrong for these files,
which hold 8-bit samples shifted left by four. Normalising by it agrees with
**none** of the 256 byte values and is off by up to 3.7e-3 -- 366 times the
1e-6 the ticket allowed -- so the check as specified would have failed on
every capture and read as a decode fault. `test_the_full_scale_that_matters`
pins both directions so nobody restores it.

The corpus is generated and never committed: `build/testfiles16/` is a
prerequisite of the check rule, and nothing in `testfiles/` is touched.

**The built program reads both corpora now, and the answer is a finding.**
`capture_sidecar.h` reads the container out of a capture's sidecar -- a
missing or silent one is the house 8-bit convention, which is what every
capture was -- so `check-pipelines` runs all six captures twice, under
"A wider container". **Every identity is unchanged**: BSIC 59, cell 28 with 2
ports, colour 17 and LA 4375, station 0x8343 `TSF`, the CPR positions. The
decoders are scale-invariant, exactly as the relative-threshold argument said.

**A block is `SAMPLE_BLOCK_PAIRS` -- 131072 pairs -- and not a byte count**,
which is what makes that true. dump1090's block was 262144 bytes *and* 131072
pairs for as long as this program had one sample container, and the day it had
two those stopped being the same number. Defined in bytes, a four-byte
container covers 32.8 ms instead of 65.5, and it cost two measured things:
LTE read 55 Master Information Blocks instead of 28 and paid **55% more
processing time** for twice as many half-length blocks, and `gsm_arfcn_69`
dropped from seven broadcast messages to two, losing System Information 3 --
the one carrying MCC, MNC, LAC and Cell Identity, which is the answer this file
pins.

The GSM mechanism is worth knowing because it is not the obvious one. Four BCCH
bursts span about 18.5 ms and fit in 32.8 ms comfortably; what does not fit is
four bursts **after the SCH**, which is `gsm_read_broadcast()`'s own refusal --
"the block ran past the end of this sample block". Eligible SCH decodes went
*up*, 9 against 7, because there were twice as many blocks; the conversion
collapsed, 7 of 7 to 2 of 9.

Both are gone. `acquisition_block_bytes()` is `SAMPLE_BLOCK_PAIRS` times the
container's width, `SAMPLE_BLOCK_BYTES_MAX` sizes the three block buffers for
the widest container this carries (`SAMPLE_MAX_BYTES_PER_PAIR`, 4 -- anything
wider is refused rather than overrunning them), and both corpora now produce
**byte-identical output**: 107 lines, every field, the only difference being
the wall clock in the ADS-B timestamps. It costs 768 KB across three buffers.
`check-pipelines` asserts the broadcast messages and the message count, so a
return to counting blocks in bytes fails there and says why. `.scratch/device-model/issues/09-*` carries the
decision it forces: "the block stays dump1090's" does not say *dump1090's
what*, its 262144 bytes or its 131072 pairs, and those were the same number
only while there was one container.

`src/device_profile.h` is the contract those tickets fill in: the facts that do
**not** transfer between receivers -- format and full scale, bytes per pair,
tuning and rate reach, the gain model, whether ppm drifts, the reference clock,
the retune settle. It is **data, not a vtable**; function pointers wait for a
second backend to satisfy them (ticket 07), because an adapter with one
implementation is a pass-through. No driver header, no GUI header, no `struct
app`, and **nothing reads it yet** -- `check-device-profile` is its only
consumer, deliberately, since tickets 03 to 06 move one area each.

**`full_scale` is carried rather than derived from the format, and that is the
finding, not a convenience.** Ticket 01's rescaled corpus and a real 12-bit
part are both `SAMPLE_FORMAT_S16` and rail at 2040.0 and 2047.5 respectively,
so a profile deriving full scale from its format would have to pick one and be
wrong about the other. `device_default_full_scale()` therefore returns **0 for
S16** -- a caller that gets it has to go and find out, which is correct, and a
default there would quietly have supplied 2047.5 to the corpus it disagrees
with everywhere.

The check pins each field against the constant it will replace --
`SURVEY_TUNER_LOWER_HZ`, `SURVEY_SETTLE_SECONDS`, `RECEIVER_REFERENCE_HZ` --
by including those headers rather than restating their numbers, so the two
cannot drift apart while both exist.

**`acquisition.h` used to be the one it could not include, and no longer is.**
That sentence stood here after ticket 07 put `<rtl-sdr.h>` behind
`backend_rtlsdr.c`, and by then **no header in `src/` included it at all** --
`app.h` only records that one used to. A translation unit including
`acquisition.h` compiles `-Wall -W` clean and links with `-lm` alone, which is
why `check-acquisition` can drive the worker at all. Left standing it was
worse than untidy: it is a *stated reason not to attempt something*, and it
was cited against building a hardware-free check over the receiver's
transitions -- work that turns out to be reachable today
(`.scratch/deepening/issues/10-*`, Phase 2). A stale refusal costs more than a
stale fact.

One thing the profile deliberately cannot say: **librtlsdr's rate range has a
hole in it** -- 225001-300000 and 900001-3200000 Hz, with nothing between --
and `rate_min_hz`/`rate_max_hz` cannot express that. Nothing here tunes into
the gap (2.0, 2.048 and 1.92 MS/s are all in the upper span), so it is left as
a known limitation rather than a representation invented for one device.

What the DSP costs, against the 65.5 ms of signal one block covers:

```sh
make bench-dsp                              # as built, no -march
make bench-dsp BENCH_ARCH=-march=native     # with this machine's SIMD
```

The answer as of this writing: the Scope path uses about 7 ms of the 65.5,
the GSM view about 22, and `-march=native` changes none of it beyond noise.

**gcc, not clang, and that was measured too.** Alternating three runs each,
clang 22 against gcc 16 at `-O3`: the GSM SCH decode 20.3 ms a block against
15.2, the LTE cell search 16.1 against 10.1, every cell on a carrier 27.6
against 20.3 -- 33% to 59% slower on every stage that matters, and slower on
the small ones too. It produces no diagnostic gcc does not, so it is not
earning its place as a second opinion on warnings either. Where clang *is*
worth running is as a second implementation, and it has now earned that
twice over: `make CC=clang check` passes **all 55 suites**, and the one it
used to fail paid for itself.

That was `check-lte-dsp`'s two-cell fixture, and this file recorded it as a
fragile check sitting at the edge of what the search can do. It was neither.
`fill_other_traffic()` drew both bits of every QPSK symbol as two `rng_next()`
calls **in one argument list**, where evaluation order is *unspecified* -- not
undefined, which is why gcc with `-fsanitize=undefined,address` had nothing to
say about it -- so the two compilers built **different carriers** and got
different answers about them. A second implementation is what tells that from
arithmetic; nothing else here could have. `.scratch/two-cell-fixture/` has the
stage hashes, and the lesson generalises past one line: **a side effect in an
argument list is a fixture that depends on the compiler**, and a suite whose
fixtures do that is measuring the toolchain.

The fix to the check is worth as much as the fix to the fixture. Once both
compilers built the same carrier, the old fixture separated two cells on
neither -- because the level was never the variable. Over forty draws of the
interfering traffic, two cells come back on 19 at equal power, 20 at -0.7 dB
and 15 at -1.4 dB, so there is no level with margin to move a single fixture
to. **The fixture is a population now**: sixteen carriers with the same two
identities and different traffic, asserting the rate against a floor of four,
and asserting absolutely that a pair reported is never the wrong pair -- 54 of
54. `make probe-two-cell` is the harness, built from the check's own
translation unit because a copy of a fixture is a second fixture: an earlier
standalone copy reported zero cells at every level, having left
`twiddles_init()` behind in the suite's `main`.

**`-O3` is the default and one stage is why.** Measured three times each,
alternating so a drifting machine cannot fake it: the GSM SCH decode goes from
20.4/23.1/21.7 ms a block at `-O2` to 14.5/15.8/15.7 at `-O3` -- about 30% off
the largest single stage, 33% of a block down to 23%. Everything else moves
within noise, the LTE cell search and the spectrum's transforms included.
Nothing was over budget at `-O2`, so this buys no capability; what it buys is
headroom, and headroom is not free here because ADR-0002 has a slow renderer
*drop* blocks rather than lag -- a machine slower than this one loses decodes,
and the biggest stage is where that starts. The whole suite passes at `-O3`,
real-capture invariants included, and there is no `-ffast-math`.
LTE is reported against its own budget, because 131072 pairs at 1.92 MS/s is
68.3 ms rather than 65.5: the cell search costs about 14 ms of it -- 11 for the
PSS correlation over 9600 offsets against three roots, the rest for the integer
frequency sweep -- and each Master Information Block attempt about 9 more, of
which the view makes up to three.
See `docs/liquid-dsp-sdrprobe-assessment.md` for the numbers and for where the
time would come from if it were ever needed.

**Scope resolution** in the Settings panel steps the Scope's transform size
through the powers of two from 256 to 16384, and says what each costs: a
longer transform buys resolution and spends averaging, because the window
count is `pair_count / size`. It is honoured **only while the Scope owns the
spectrum** — `input_scope_owns_spectrum()`, asked every block rather than
reset on a screen change, because the survey, both band scans and the
calibration overlay read the same array and their floors were chosen against
977 Hz bins. Calibration is an overlay, not a tab, so a test on the tab alone
would hand it whatever the Scope had chosen. `--fft points` sets it from the
command line.

**The Scope header has a stepper for the same value and it changes this run
only; the Settings panel is the one that persists it** to `fft_size` in the
config. One click on a chart is easily an accidental one and should not decide
what the program opens with tomorrow, whereas reaching Settings takes an
overlay and an Apply. Applying in Settings keeps whatever the header last
chose, so the two never fight -- and `config_set_fft_size()` has exactly one
caller, which is what keeps that true.

`--analysis` opens a decode view on its charts rather than its data — one flag
for all of them, since every decode view has the same two arrangements. The
TETRA view's analysis arrangement draws the phase steps and how much of each
255-symbol slot repeats; its header carries the funnel, because bursts without
parity is a coding fault and no bursts at all is tuning or band.
`--view calibration` opens the calibration overlay, and `--calibrate lte`
alongside it opens on the 4G arrangement. Every screen has to be reachable from
the command line for the same reason every decision does: the LTE calibration
panel shipped with three overlapping regions because there was no way to look
at it.

Seeing what a view drew, without a person to look — `--screenshot` writes the
last frame to a PNG the Read tool displays. The `screenshot` skill in
`.claude/skills/` carries the per-view recipes and how long each needs to
settle:

```sh
./sdrprobe --file testfiles/lte_b20_pci28.bin --view lte --earfcn 6200 \
    --duration 6 --screenshot /tmp/shot.png
```

A screenshot is for seeing that something drew; for reading values off it the
headless paths below are exact and do not truncate.

Running the app without hardware — always prefer this over asking for a dongle:

```sh
./sdrprobe --file testfiles/adsb_modes1.bin   # paced, looping playback
./sdrprobe --view adsb --duration 20          # open on a screen, quit by itself
```

Checking a capture decodes, with no window and nothing to click — the fastest
way to see whether a change to the DSP helped or hurt:

```sh
./sdrprobe --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode --once
./sdrprobe --file testfiles/adsb_cpr_pair.bin --headless --technology adsb --decode --once
./sdrprobe --file testfiles/tetra_cc17.bin --headless --technology tetra \
    --sample-rate 2000000 --decode --once
./sdrprobe --file testfiles/fm_rds_tsf.bin --sample-rate 2048000 \
    --frequency 89.5M --headless --technology fm --decode --once
./sdrprobe --headless --record-seconds 2 --technology adsb   # live capture + sidecar
```

Walking an LTE band without a window -- the scan is otherwise a button, and a
button is not something a script can press:

```sh
./sdrprobe --headless --lte-scan 20        # bands 8, 20, 28; ~170 s for a band
```

One `cell` line per identity found, then a summary. The sweep takes three
looks at every channel and lists an identity that repeats; a confirmation pass
then revisits each entry with five more looks and drops any that cannot say the
same thing twice, which the summary reports as `dropped`. The `cell` line
carries the PSS correlation and the SSS margin, so a weak survivor can still be
told from a solid one.

Reading a band survey without a window — the only way an agent can see what
the survey found, since a sweep is otherwise reached by clicking:

```sh
# a capture holds one tuning, so its survey is one step and repeats exactly
./sdrprobe --file testfiles/gsm_arfcn_69.bin --frequency 948.4M --headless \
    --survey --once
# a receiver sweeps whatever range it is given
./sdrprobe --headless --survey --survey-range 470M:690M --survey-dwell 0.2
```

Surveys accumulate rather than scroll past: `scripts/survey_tool.py` turns that
output into a JSON under `surveys/`, reports one grouped by allocation, and
diffs two -- refusing outright when the two were taken at different sites. The
data is gitignored, the same as `captures/`: a sweep is one location with one
antenna and nobody else's baseline. `docs/band-surveys.md` is the format, and
the `rf-environment` skill is the analysis over them -- what is known, what is
new, and the two gates a candidate technology has to pass before anyone writes
a ticket for it.

The antenna and the site persist between runs (`--antenna`, `--site`, kept in
`~/.config/sdrprobe/config`), because they describe the installation rather
than a run and a survey that cannot say what it was taken with is not
comparable to anything. The survey view has both as fields and a **Save
survey** button beside them, writing the same JSON the script ingests; it
refuses while the site is empty rather than saving a sweep labelled nothing.

**Watch** keeps the survey sweeping, folding each sweep into the site's history
and reporting what appeared and what went quiet (`--survey-watch <n>` from a
script). It is what makes the history worth having: the history counts, per
signal, how many of a site's sweeps in each hour heard it, so a signal whose
absence follows the clock is `by hour` rather than merely `on/off` --
a distinction no single sweep and no count of sweeps can reach.

**The survey is a tab of its own, first, and the screen the application opens
on.** It was a fifth Scope view reached by a button, and that put it inside
the wrong thing: the four Scope views draw whatever the receiver is pointed at
and this one walks the receiver across a band. Under Scope it also inherited
Scope's numbered options — 1 magnitude, 2 spectrum — which name screens it has
nothing to do with. `enum active_tab` is Survey, Scope, Decode.
Its candidate list carries each maximum's width and shape (`src/survey_carrier.h`)
and what the site has heard of it -- new, steady, on/off, gone
(`site_history_seen()`).

**A candidate's mark says which of four things it is**, in the chart and in
the list, because one filled dot for everything made a spur, an empty
frequency and a broadcast station identical on the screen where telling them
apart matters most. A **filled dot** is a candidate with nothing known against
it; a **cross** (`*` in the list) is the receiver's own comb; a **hollow dot**
(`~`) is a frequency where the confirmation pass found a prominence and
nothing else; and a **cross with a dot in it** (`*!`) is on the comb *and*
reads displaced, so something real is there. That fourth shape exists because
the chart draws one mark per peak and both obvious resolutions are wrong -- a
plain cross tells a reader to stop looking at the one candidate they should
look at, and a plain dot silently discards a mark the operator has learned to
read (`.scratch/reading-origin/issues/01-*`). `sdrgui_survey_peak_mark()` is the precedence -- empty wins over
receiver-like, because "there is nothing here" is what a reader acts on -- and
the caption counts what it drew rather than what the sweep thought, because a
caption that disagrees with the picture above it is worse than none.

`docs/receiver-artifacts.md` is the reference for those marks: every algorithm
behind them, every formula, every adjustable parameter and what constrains it,
with a worked example of each. Three of the parameters are not free --
Rayleigh's 0.5227 is `sqrt(4/pi - 1)` and changing it means comparing against
something that is not noise, `READING_SEPARABLE_TOLERANCES` is the condition
for two intervals to be disjoint rather than a threshold, and
`RECEIVER_COMB_MAX_FRACTION` bounds every comb tolerance, where loosening it
produces flags with no evidence rather than more flags.

**Where a candidate reads is the second kind of evidence, and it contradicts
the comb as often as it corroborates it** (`src/reading_origin.h`). An
uncalibrated receiver does not report a frequency vaguely, it reports it wrong
by a known amount: with a crystal error `k` and a correction `c` in force, a
tone at true frequency `f` comes back at `f/(1 + k - c)` -- the tuning cancels
out of that exactly, since one crystal clocks both the synthesiser and the ADC.
A tone generated from the receiver's own reference is at `f_nom*(1 + k)` to
begin with, so uncorrected it reads at **exactly its nominal** however far out
the crystal is. The two hypotheses are `f*k` apart -- about 4.1 kHz at 132 MHz
here -- and **the correction does not narrow that by one hertz**; it only
swaps which of them reads on the nominal. One subtraction, no second room and
no second receiver.

**The sign is the negation of the number `cal-measure` prints**, and ticket 11
had it backwards. `observed_ppm -31.84` is the *residual*,
`(measured - expected)/expected`; the crystal error is `+31.84`, so this
reference is **fast** and an uncorrected reading of a real transmitter comes
back *low*. Every "reads exact, therefore clocked here" verdict survives the
correction -- being at the nominal cannot care which way the other hypothesis
lies -- but the airband's one external carrier is on 132.066667 rather than
the 132.058333 the ticket names.

It settled three things nothing else could. 135.000 MHz was recorded as "a
real AM carrier" and reads 61 Hz from exact where a transmitter must read
4.2 kHz off, so **four** of the airband's six strongest signals are the
receiver rather than three. 150.0009 MHz fits no modelled comb and still reads
exact, which is `SURVEY_SUSPECT_UNEXPLAINED` -- the flag that stops
"unremarked" meaning both "asked and answered nothing" and "nobody asked". And
94.4 MHz, the loudest FM station here, sits on the fine comb by coincidence and
reads 2.9 kHz **high**, so the coherence test declines to flag what the comb
flags -- the comb's mark is left standing, because this adds evidence rather
than silently overruling a mark an operator has learned to read.

**A crystal error of zero is a refusal, and a calibrated receiver is not in
that case.** It is zero for a receiver nobody has calibrated (the error is
unknown) and for a capture (a file does not carry its recorder's crystal). The
first version of this took **one** number, `calibrated - applied`, reasoning
that a corrected receiver has nothing left to displace anything by -- and that
made the whole measurement **dead code in the shipping program**, which
restores and applies a stored calibration at startup, while every unit check
stayed green because a unit hands the number in. Correcting the ppm *inverts*
the discriminator rather than removing it, so both numbers are inputs. The
tolerance is
**one bin of whatever measured the candidate** and emphatically not
`RECEIVER_COMB_TOLERANCE_HZ`: 25 kHz is cheap against a 14.4 MHz comb spacing
and would not loosen this test but abolish it, wanting a carrier at 1.6 GHz
before any verdict was available, with a green suite throughout.

**Three grids are asked, and a service raster answers only half the
question.** The two combs and the octave chain (`src/clock_chain.h`, f/2f/4f
and never 3f) may answer both hypotheses; a **channel raster** from the band
plan may answer only *external*, because a channel grid says where a
transmitter may sit and "a tone clocked by this receiver that happens to land
on an airband channel" is not a hypothesis anybody holds. The arithmetic is
`RECEIVER_COMB_MAX_FRACTION`'s with different numbers: at 8333 Hz spacing and
a pass's 977 Hz tolerance a reading lands within tolerance of *some* channel
**23% of the time**, against 0.12% for the 1.6 MHz comb. It was found on air
-- a noise maximum refuted 0 of 6 came back `clocked-here` because its centre
sat 433 Hz from where a coherent source on airband channel 1990 would read.

**The raster lives in its own table** (`struct band_plan_raster`), not as two
fields on all eighty `band_plan_entry` rows -- which would each carry two
zeroes to stay `-Wall -W` clean and bury the one that matters. Only an
allocation with **no decoder** may have one: `gsm_arfcn_hz()` and `fm_scan.h`
already own those grids, and a second statement here could disagree with the
module that decodes it. `check-band-plan` asserts both -- every raster names a
real allocation by its exact lower edge, and that allocation has no decoder.

**It was verified on air rather than by the suite**, which is the only thing
that could have caught either fault. With `calibration ... 32 ...` in force a
128-137 MHz sweep read the three known comb families at 131.204163, 129.604553
and 136.005188 against a model predicting 131.204198, 129.604147 and
136.004352 -- **35, 406 and 836 Hz**, each having moved four kilohertz from
where the uncorrected pass found it. The same run is what showed
`SURVEY_SUSPECT_UNEXPLAINED` firing on five refuted noise maxima: a noise peak
is narrow, so it carries `UNRESOLVED` like a tone does, and the flag now wants
a carrier and a verdict that is not `refuted`.

`docs/what-is-on-air.md` is the assessment over all of it: every allocation,
what this program does about each, and where something was ruled out the
measurement that ruled it out. `.scratch/calibrating-the-flags/` was the open
question underneath it -- every threshold behind those marks was measured on
one dongle at one site and compiled in -- and it is **absorbed into
`.scratch/device-model/issues/08-*`**, where the comb half is now done: the
reference is `device_profile.reference_clock_hz` rather than a constant, and
`survey_comb_spacing_hz()` derives the comb from whatever it is handed.

**A source with no clock gets no comb tests at all**, which is the case a
capture is in: whichever receiver recorded a file had a crystal, but the file
does not, so nothing may attribute a comb to it. The two divisors -- a tone
every reference/2, a finer one every reference/18 -- were **measured on an
RTL2832U and are unverified anywhere else**; they are facts about that chip's
clock tree, not about reference oscillators, so measure before trusting them
on another part. Everything else in that table is relative -- a dB over a
local floor, a percentile, a fraction -- and transfers untouched.

The site and the antenna are combos over lists the configuration keeps
(`config_remember_site()`, `config_remember_antenna()`), because one place or
one antenna named two ways is two of them and levels only compare within one of
each. The antenna defaults to `telescopic`.

**Band...** beside the range fields fills them in from the band plan rather
than from memory: the allocations **this receiver** can reach, with the ones
that have a decoder behind them picked out, and a dwell chosen to suit the
width — half a second for anything under about fifty megahertz, down to the
default for the whole tuner. `src/survey_bands.h` is the arithmetic and
`check-survey-bands` asserts that nothing offered is out of the tuner's reach
and nothing reachable is left off.

**The reach is the profile's, and it is a filter rather than a fact about the
program.** 54 allocations on an R820T, 60 on a 70 MHz – 6 GHz part, and
**neither list contains the other**: a wideband device opens 2.4 GHz ISM, 5G
n78 and the 5 GHz RLAN bands, and loses everything below about 70 MHz — short
wave, CB, the 6 m and 10 m amateur bands, band I television. That asymmetry is
why `check-survey-bands` runs its both-directions property against *two*
profiles: a check against one device's numbers passes while the list offers
half of one and misses half of the other. The band plan itself now runs to
5875 MHz whatever is plugged in; it says what a band is *for*, and reachability
is a separate question (ADR-0015).

Under file playback the picker offers **one** allocation, the one the capture
sits in, because a capture's tuning range is the single frequency it was taken
at. That is deliberate — a sweep needs a live receiver and the view says so.

A sweep's peaks are grouped into signals by `src/survey_carrier.h` before
anything reads them: two maxima are one carrier when the power between them
never drops far below the lower of the two, and each carrier's extent runs to
the trough on either side rather than to a fixed number of decibels down --
which is what gives a weak peak a width that means something. Its `centre_hz`
is the middle of that extent and identifies the signal; `power_centre_hz` says
where the energy sits, and the two part company on a lopsided carrier.

**The tuning correction is kept per receiver *and* site**, and the survey
history per receiver, site *and* antenna — `src/installation.h`, ADR-0018 and
ADR-0022. A correction drifts and is measured against whatever reference a
place offers, so arriving somewhere the receiver has been calibrated restores
that calibration rather than the last one measured anywhere; and it
compensates *one crystal*, so a second receiver at the same site keeps its own.
Swapping a whip for a rooftop makes known carriers vanish and new ones appear,
and a history that could not tell that from a change on air would report it as
one. Gain is deliberately in neither key, so a presence claim survives an
ordinary gain adjustment.

**A value from before those ADRs is kept and not applied.** `known_site <ppm>
<label>` is the legacy shape and stays readable; `calibration <receiver> <ppm>
<site>` is the new one — a separate line, so an older build still reads the
file and so a legacy value stays visibly legacy rather than being silently
given an owner. **The survey history's twin of that promise was never built,
and ADR-0022 was amended on 2026-09-11 to say so**: `surveys/history-<site>.txt`
is inert, not an unassigned baseline waiting to be claimed. The program builds
one history name, `installation_history_path()`'s, so nothing opened a legacy
file and nothing offered one -- the three by-site entry points had no caller
outside `tests/` and are deleted with the clause. An operator who knows a
legacy file's provenance renames it, which puts the assertion where the
knowledge is; `check-installation` asserts the file stays inert beside a
receiver-scoped one. Claiming is the operator's
explicit act: `--claim-calibration`, with `--receiver-label` for a receiver
whose USB serial is missing or shared, which many of these dongles are. A
receiver with no identity is told so rather than offered a claim it cannot
make. `installation_commit()` is the one writer, where four call sites each
used to decide when a save was due. A sweep's JSON names the receiver too
(`receiver.id`, `null` when there is none), because a sweep that cannot say
what took it cannot be matched to a calibration or a baseline — and a
headless survey of a capture now says *"survey-history not kept"* rather
than reporting a baseline it did not write. A sweep's marks are claims from a tenth of a second each;
**Ask again**, or `--survey-confirm` on a scripted sweep, revisits each with six
blocks on the frequency, each measured on its own, and prints a verdict with
the count behind it -- `confirmed` when it was up in every look, `refuted` when
in none, and **`intermittent`** in between, which the saved JSON records per
signal alongside `unconfirmed` for anything nobody asked about. Intermittent is
the answer the mobile-satellite bands need: on 1600-1670 MHz a pass put four of
seven signals there, and every one of them would previously have been refuted
and barred from the site history for ever. The window asks
only about what changed, since it has a history to lean on; a headless sweep
asks about **every signal it found**, because its output is the report. Above
1.5 GHz that is most of the answer -- one 1400-1766 MHz sweep found ten signals
and the pass confirmed one, while the same flag over band II confirmed all
twenty-four broadcast stations. The site is a combo: type a new
one, or pick one this receiver has been to before, from the list `config_remember_site()` keeps -- spelling one place two
ways makes it two places and nothing downstream can tell. Saving also folds the
sweep into `surveys/history-<receiver>-<site>-<antenna>.txt`
(`src/site_history.c`), which is what
lets the window tick the candidates this site has never heard, mark where
something it knows has gone quiet, and say under the cursor how many sweeps
ago. Matching uses the coarser of the two sweeps' bin widths; the reason is in
`site_entry.bin_hz` and it is not optional.

**A finished survey is a record before it is a screen or a file**, in
`src/survey_record.{c,h}`. Both the window's save and the headless report build
one from plain facts and then format it, so what a candidate *is* -- where it
was found, what it measured to, whether it resembles the receiver, which
allocation it falls in, and what a confirmation pass concluded about it -- is
decided once. It used to be decided inside `survey_store.c`'s `printf` loop,
where the two adapters could have disagreed about the same peak with nothing to
say so, and where the module that spells JSON owned the meaning of a candidate.

The measurement that said so was the store's own check: it allocated a whole
`struct app` to write one file. It builds a record now, `survey_store.{c,h}`
has no `struct app` in it at all, and `check-survey-store` links `-lm` alone
where it used to need raylib and librtlsdr headers. Both outputs are
byte-identical over the capture survey, the JSON apart from its clock.

**What the record decides, and the adapters only print**: that a candidate
takes the verdict of the carrier holding it -- a carrier's shoulders are maxima
of the same signal a few bins away, so asking at each maximum's own frequency
would leave most of a confirmed station's list unconfirmed -- the suspicious
and confirmed/intermittent/refuted totals, and the half-bin tolerance every
match is made with. `survey_tuning_from()` in `view.h` is the one place the
four facts a candidate needs are read out of `struct app`: where the receiver
was pointed, how fast it sampled, its reference clock and whether DC was
removed. Three sites used to assemble those separately.

Two things the modelling turned up and the checks now pin. **The dwell written
to a file is the session's, not the plan's** -- `struct survey_plan` carries a
`dwell_seconds` and the writer has always taken
`app->survey.session.dwell_seconds`; they agree on any sweep this program plans
for itself, and a record reading the plan's would change the file while every
other field agreed. And **a source with no clock gets no comb tests**: the
tuning carries 0 for a capture, and a default of 28.8 MHz there would flag a
recording's maxima as a receiver's own spurs with no receiver in the room.

**And the sweep itself is one machine now, in `src/survey_session.{c,h}`:**
idle to sweeping to confirming, with watching as a sweep that goes round again
and measuring as a look at one candidate. `view_survey.c` draws it and turns
clicks into intents; `survey_report.c` prints it. Two refusals give it its
shape and both are what let the two copies drift apart before it existed: **it
does not touch the receiver** -- it says where it wants the tuning and the
adapter obeys, then reports whether the tuner moved
(`survey_session_retuned()`) or would not -- and **it does not read or write
files**, holding a `struct site_history` and saying when what it holds changed.
`struct survey_block` is the seam, so nothing in the machine sees `struct app`.

**Four things the two copies had disagreed about**, none reachable by any
check, because `make check` never runs a sweep and the answers a capture pins
are the ones a broken sweep does not change. The headless sweep **folded every
block it consumed**, settle or not -- about a third of everything it measured,
written into bins at frequencies nothing was transmitting on, while the window
had always obeyed the rule. A watch reported **`carriers 0`** for its first
sweep, because it folds the sweep in and clears the array to go round again
before an adapter reads the count (`watch_carriers` is the count belonging to
the sweep that was folded). The **`# confirm` header disagreed with its own
rows** -- the headless one promised five fields where its rows carried seven,
and the window printed no `kind` line at all. And the window handed
`spectrum_average` to a save, which every block rebuilds from scratch, where
the headless path handed a **peak hold over the whole capture**:
`survey_session_spectrum()` is the hold, and it refuses across a swept range
because there it belongs to whichever step was last.

**And five the extraction itself broke**, which is the argument for measuring
a refactor rather than trusting a green suite. Two came back from a live sweep
alternated against the old binary: **the settle is timed from the tuning, not
from the request** -- a retune flushes the pipeline and costs about a tenth of
a second, the whole of `SURVEY_SETTLE_SECONDS`, so timed from the request the
same band II sweep reported `settling 0` where it should report 13 -- and **a
step is over on its own clock once it has heard something**, where returning
early on "no block" cost a block a step, 39 over a 13-step sweep against 26.
That is why `update_survey()` is called **every frame** with
`spectrum_updated` as a parameter rather than as a guard: a *look* counts
blocks, because counting frames gave the confirmation pass six looks in a
tenth of a second, and a *step* counts time. The other three were one mistake
made three times -- reaching for `survey_session_clear()`, which forgets the
sweep, where the thing to forget was the measurement. The worst of them had
**Reset zoom restore the kept sweep and then empty it**, and that is what took
the narrowing snapshot into the session too (`survey_session_keep()` /
`_restore()`), where the order of the forget and the copy is written down and
checked -- and where the restore turned out never to have put the sweep's
*plan* back, so a wide sweep's peaks were being read with a narrow sweep's bin
width.

The survey view also read the site history **off disk on every folded block**,
because the marks were refreshed inside the peak finder and the peak finder
ran during the dwell. The marks come from the history the session holds now,
and the file is read when the site changes or a sweep ends.

**Measuring one candidate has the same settle, and did not.** Selecting a
candidate retunes the receiver, and the blocks already in the pipeline hold
the previous tuning's samples -- so peak power, prominence, bandwidth and duty
were all being computed partly from wherever the receiver had just been. A
spectrum *average* blurs one stale block among the good ones well enough that
it never showed. A carrier measurement cannot: the first live run of
`signal_findings` called the 75.000 MHz clock harmonic "a modulated carrier,
19 dB up" where the same signal recorded and measured offline reads 40.7 dB
and 87% standing still. `survey_measure_settled()` is the rule and
`check-survey-sweep` asserts it.

A sweep also throws away every block that arrives before a step's settle is
over -- it was in the pipeline while the tuner was moving, so it holds the
previous step's samples, and folding it writes that step's signal into this
step's bins. `survey blocks 270 settling 135` says how many; at a 0.10 s settle
and a 0.10 s dwell it is about a third of what arrives.

One record per line, keyword first, integer hertz, the band-plan allocation
last because it is the only field that can contain a space. `candidate` rows
carry the frequency the survey found, the frequency the measurement refined it
to, the width, and any suspicion flags; `survey` rows carry the plan and the
totals.

Built binaries (`./sdrprobe`, `build/`) and `captures/` are gitignored.

## Architecture

### Two bounded contexts, one window

`CONTEXT-MAP.md` splits the domain in two, and the split is load-bearing for
naming: the **Probe** context (`CONTEXT.md`) acquires samples and stops at signal
measurements; the **Decoder** context (`docs/contexts/decoder/CONTEXT.md`) owns
the interpretation of standardized modulation as transmitted information.
Tabs are presentation only, not the boundary (ADR-0010, ADR-0021).

### DSP: generic core + technology modules

- `src/signal_probe.{c,h}` (`signal_`) — what a signal is, for a signal nobody
  has identified. The rule for what belongs here is one line and it is what
  keeps it from becoming a junk drawer: **a measurement belongs in
  `signal_probe` when it needs no sequence.** Oerder-Meyr symbol timing needs
  none, a cyclic-prefix autocorrelation needs none, a Zadoff-Chu correlation
  needs the sequence and stays in `lte_dsp`.
  **`signal_find_carrier()` had a comb of blind frequencies until 2026-09-11**,
  and it is the shape of fault this file keeps warning about: its coarse grid
  stepped `rate/probe * 4`, four times the main lobe of the mix it was
  probing with, so a line one or three lobes off a grid point fell in a null
  at *both* bracketing probes. A **noise-free** tone at 120 010 Hz was lost
  outright, answered 58 kHz away, while the same tone at 120 000 Hz was found
  exactly -- and nothing caught it because every synthetic fixture used
  120 000 Hz, which lands on the grid, and the real captures landed elsewhere.
  The grid is `rate/coarse` now over a `SIGNAL_COARSE_PAIRS` prefix, which is
  both correct and 3.5 times faster; the constant is measured where it breaks
  and the header says how.
  Today: where a carrier is and whether anything rides it
  (`signal_find_carrier`, and the two names are careful -- neither
  `carrier_over_noise_db` nor `carrier_power_fraction` is a term of art, and
  the header says which standard idea each is *not*); the symbol-rate line at
  a given rate (`signal_symbol_line`, which `tetra_symbol_timing` now wraps);
  a burst grid from decided symbols (`signal_repeat_find`, wrapped by
  `tetra_burst_find`); and folding at a period
  (`signal_lag_correlation`, `signal_fold_at`, which `probe-periodicity` now
  calls rather than carrying its own).
  **There is deliberately no blind search for the symbol rate**, and the
  reason is measured: scored against a local floor it finds both TETRA
  captures at 17998 Bd and 37.5 times their floor, and a 25 kHz slice of a GSM
  capture at 28.6 -- where 3466.9 Bd is twice GSM's burst rate. Oerder-Meyr
  detects periodicity in the squared magnitude and a burst grid *is*
  periodicity in the squared magnitude, so asked blind it cannot tell a symbol
  rate from a frame rate. `.scratch/signal-probe/issues/02-*.md` has the table.
  `signal_find_bursts()` is the time-domain half: how long a burst is, how
  often, and what fraction of the look is occupied, at sample resolution
  rather than the survey's 65.5 ms block -- which cannot tell a 120 us
  squitter from a carrier that never stops. Three verdicts, because "nothing
  to report" has two causes: **level** (a carrier, or an empty channel),
  **busy** (a transmitter that has not stopped) and **separable**. Every
  constant in it was measured and three were wrong first: thresholding a
  *raw* envelope reported 134726 bursts in a bare carrier; a 99.9th-percentile
  ceiling could not see Mode S, whose frames are 0.04% of the buffer; and
  contrast cannot separate an LTE downlink's OFDM symbols (191 "bursts",
  20.5 dB) from Mode S (4 bursts, 26.1 dB) -- occupancy does, 0.796 against
  0.0037. Lengths are long by exactly the smoothing window and it is
  subtracted; synthetic bursts of 100, 300 and 1000 us then read exact.
  `signal_envelope_stats()` is the envelope's shape, in the channel mixed to
  zero and filtered to its own width -- **that isolation is the measurement**,
  since across a 2 MHz span the envelope of a narrow signal is the envelope of
  the noise beside it. Read against Rayleigh's 0.5227, which is what complex
  Gaussian noise gives and depends on nothing: FM broadcast reads 0.032, a
  bare carrier 0.137-0.248, TETRA 0.25-0.27, an empty channel 0.545, Mode S
  1.057 and an LTE downlink 1.098. It is a scale and not a classifier -- a
  bare carrier in noise and filtered pi/4-DQPSK read the same number -- and it
  measures the envelope over the *look*: GSM is constant-envelope by
  construction and reads up to 0.79, because it is also TDMA. **The
  instantaneous-frequency histogram that would name FSK is deliberately not
  built**: an empty 25 kHz channel spreads 8.7 kHz against TETRA's 5.0 in the
  same channel, so the noise is wider than the signal and its modes would be
  the noise's.
- `src/signal_frame.{c,h}` (`signal_frame_`) — one sample block, converted
  and measured, and the only owner of what comes out of it: centred I/Q and
  magnitudes, their min/mean/max, the signal statistics, the DC-filtered copy
  the **spectrum** is taken from -- never the samples a decoder reads -- the
  transform, its peak hold, and whether any of it is ready. This was
  `process_block()` in `sdrprobe.c` leaving twenty-odd loose arrays, counters
  and ready flags on `struct app` for every view, overlay, session and
  headless path to read directly; `check-sdr-dsp` proved each primitive and
  **nothing proved their composition**, which is where a peak hold survives a
  change of transform size or a decoder is handed filtered samples. It knows
  nothing about what is on screen: the transform size is an argument, because
  `input_scope_owns_spectrum()` is a question about presentation, and a change
  of geometry is *reported* rather than acted on -- the frame drops its own
  peak hold and the Scope drops the waterfall's rows, because those rows are
  not the frame's to clear. `process_block()` survives as the thirty lines of
  application policy that decide the size and act on that report.
- `src/signal_findings.h` — one layer over that, and the same relation to it
  that `lte_findings.h` has to the LTE measurements: sentences with their
  numbers attached, and refusals where the measurement cannot reach. It is
  drawn on the survey's candidate panel **above the band plan**, and the
  order is the argument -- a reader who has already read "Aeronautical
  radionavigation -- ILS markers" reads everything after it as detail about a
  beacon, so the measurement has to come first to be believed over the label
  (ADR-0015). On air, 75.0005 MHz reads *a bare carrier, 42 dB over its
  floor; 87% of the channel stands still; nothing rides it* under exactly
  that allocation, and 100.2965 MHz reads *a modulated carrier, 56 dB over
  its floor; almost none of the channel stands still; no symbol rate looked
  for: needs one channel*. **It must not become a verdict**: "18 kBd, 25 kHz
  wide, continuous" lets a reader reach for the TETRA view, and "probably
  TETRA" is a claim nothing here can stand behind.
- `src/sdr_dsp.{c,h}` (`sdr_dsp_`) — technology-independent primitives: byte→float
  I/Q, DC removal, peak binning, signal stats, a hand-written 2048-point
  Hann-windowed FFT → dBFS, power centroid, channel-power reducer, PPM.
- `src/gsm_dsp.{c,h}` (`gsm_`) — GSM 900: ARFCN map, FCCH tone detector, SCH
  decoder (differential GMSK demod → training-sequence sync → rate-1/2 Viterbi →
  parity → BSIC + frame number).
- `src/gsm_bcch.{c,h}` — one layer further, and on the Decoder side of the
  context map: four normal bursts → deinterleave → rate-1/2 soft Viterbi →
  (224,184) Fire code → a System Information message (MCC, MNC, LAC, Cell
  Identity). Fed by `gsm_normal_bursts()` in `gsm_dsp.c`: coherent detection,
  a five-tap channel estimate, residual-offset removal and an equaliser. On
  `testfiles/gsm_arfcn_69.bin` it reports MCC 268 MNC 03, LAC 4010, CI 5131.
- `src/adsb_dsp.{c,h}` (`adsb_`) — Mode S: preamble detect, PPM bit demod, CRC-24,
  DF17/18 field parse, CPR position with an even/odd pairing cache.
- `src/lte_dsp.{c,h}` (`lte_`) — LTE cell search: EARFCN map, PSS correlation
  (Zadoff-Chu, three roots), SSS detection, cyclic-prefix length, frame
  boundary, and a frequency offset measured twice — coarsely from PSS, then
  from the reference signals of slot 1. **It runs at 1.92 MS/s and refuses
  anything else (ADR-0014).** Two traps are worth knowing before touching it:
  the PSS exponent's sign is load-bearing and **negative** -- conjugating a
  Zadoff-Chu sequence of this length swaps roots 29 and 34, a synthetic round
  trip cannot see it, and it has already been flipped once in error to make a
  broken SSS detector agree; and the SSS is detected *differentially*, each
  subcarrier against its neighbour.

  The third and worst trap is the frequency offset. **The PSS measures it as a
  phase, and a phase wraps every subcarrier** -- so what comes back is the
  offset modulo 15 kHz. An uncalibrated dongle is two subcarriers out at
  800 MHz; the PSS still locks at 0.8 with all of that present, while the SSS
  and the reference signals read subcarriers two places from where they should
  and return a confident wrong identity. `lte_cell_search` sweeps integer
  offsets to find the rest, and then re-finds the PSS peak with the offset
  removed, because a frequency error *moves* a Zadoff-Chu correlation as well
  as weakening it.
  Two measurements sit beside the identity rather than after it.
  `lte_reference_power()` is 36.214's RSRP, carrier RSSI and RSRQ over the six
  central resource blocks -- **RSRP in dBFS and not dBm**, so it compares cells
  on this receiver and nowhere else, while RSRQ is a ratio through the same
  chain and transfers anywhere. The reason is **not** the antenna's gain, which
  it was long described as: 36.214 puts RSRP's reference point at the UE's
  antenna connector, so what is missing is an absolute power reference for the
  converter -- dBm at the input per full-scale sample, at a given frequency and
  gain -- which no device here ships with and which is a one-time measurement
  in the same family as the ppm calibration
  (`.scratch/device-model/issues/06-*` and `08-*`). The antenna is a separate
  objection that survives such a calibration: a whip and a handset's internal
  antenna intercept different fractions of one field, so even a correct dBm at
  this connector is not a handset's RSRP.
  `lte_channel_shape()` reports the channel's delay, its spread and the
  frequency drift left after the search's own correction. The delay is the
  phase slope across references scaled by `LTE_FFT_SIZE/6`, which is srsRAN's
  `chest_dl_estimate_correct_sync_error`; the spread is the scatter about it
  with the noise removed using the RS-SINR above, which is why the two were
  built in that order. The trap is the drift: port 0's references appear at
  symbols 0 and 4 **with their shifts swapped**, so comparing reference m
  against reference m compares different frequencies and reads a delay as a
  Doppler -- each is compared against the sum of the two bracketing it
  instead. On air, about two microseconds of spread and a few tens of hertz of
  drift; zero spread on a synthetic buffer built without delay.
  `lte_port_coherence()` says how many antennas the cell is transmitting on,
  from the reference *phases*: a reference symbol has unit magnitude, so
  dividing by the expected sequence leaves the level alone whether the
  sequence was right or not, and only the phase separates a silent port from a
  live one. Eleven differences per port put chance at 0.30. It is what
  identified the band 8 cell as four-port, and it corroborates the count in
  the broadcast's parity mask while sharing no code with it.
- `src/lte_chain_analysis.{c,h}` — the public LTE chain walk, once, for a live
  receiver and for a capture. `lte_cell_search_all`, the primary chosen by
  strongest **correlation**, every other identity's own broadcast channel, the
  primary's channel shape, port coherence and reference power, its Master
  Information Block under the three combining hypotheses, and the run's
  tallies (`lte_confirm`), statistics (`lte_stats`) and repeat rule
  (`lte_mib_repeat_observe`). No `struct app`, no acquisition, no file, no
  stdout, no private `lte_dsp.c` symbol.
  It exists because `--lte-chain` and `probe-lte-chain` implemented the same
  walk twice and drifted twice — the repeated-message rule was written out in
  both, and `lte_cell_search_all` reached a committed capture only after the
  live-only path had become able to return **fewer** cells than the
  single-cell search it generalises. **`probe-lte-chain` still walks its own
  cell from `lte_cell_search`**, deliberately: every white-box diagnostic it
  exists for is measured against that cell, and the two agree on both
  committed captures. `lte_session` is **not** a third adapter and Phase 5 of
  the ticket says why — it latches one cell for a view at 68.3 ms a block,
  where this pays for a multi-cell search and up to three broadcast attempts
  per identity, so sharing would need a mode flag and would put the
  multi-cell cost in the interactive path.
- `src/lte_mib.{c,h}` — one layer further, Decoder side: 480 soft bits →
  descramble (four offsets, since one transmission does not say which quarter
  of the 40 ms period it is) → rate dematch → tail-biting rate-1/3 Viterbi →
  CRC-16 masked by the antenna-port count → a Master Information Block.
  `src/lte_gold.h` holds the length-31 Gold sequence both sides need.
- `src/lte_turbo.{c,h}` and `src/lte_transport.{c,h}` — experimental
  groundwork for the transport layer above the MIB, **with no consumer and
  outside the supported Decoder outcomes**. Built for System Information Block 1:
  SIB1 does not fit this receiver, because the cell is 50 resource blocks and
  1.92 MS/s sees six of them, so the control message that locates it cannot be
  assembled (`.scratch/lte-sib1/spec.md`). Kept because turbo coding and
  CRC-24 are the transport layer of every LTE shared channel. The turbo code is rate 1/3 with a quadratic
  permutation polynomial between its two encoders, decoded max-log-MAP;
  `lte_transport` is the layer between that codeword and the air — CRC-24A,
  the filler bits, the 32-column sub-block interleaver and the circular
  buffer. **Both constants transcribed from the standard are checked against
  properties rather than against their own use**: a QPP is a permutation
  exactly when f1 is coprime with K and every prime factor of K divides f2,
  and a CRC register fed its own polynomial must leave no remainder. A wrong
  table that both sides share round-trips perfectly and fails only on air.
- `src/tetra_dsp.{c,h}` (`tetra_`) — TETRA, one 25 kHz carrier down to dibits
  (`.scratch/tetra-network-identity/`). Two things here are unlike everything
  else: **the symbol rate does not divide the sample rate** — 18 000 into
  2 000 000 is 111.11 samples per symbol, where `fm_dsp` gets to pick a whole
  decimation because a station transmits its pilot precisely so a receiver
  needs no blind loop — so the timing is recovered from the symbol-rate line in
  the squared magnitude (Oerder-Meyr), which is the *same statistic* that says
  the carrier is TETRA at all. And **the modulation is differential, so
  absolute phase is irrelevant and a residual frequency offset is still
  fatal**: the four legal phase steps are 90 degrees apart, so a rotation does
  not blur the constellation, it turns every dibit cleanly into a different
  one. It is measured coarse then fine, the fine stage by taking the fourth
  power — every legal step is an odd multiple of pi/4, so four times any of
  them is -1 whatever was sent, and the data cancels itself.
  `lock` is the answer to "is this TETRA": 0.80 on the carrier here against
  under 0.035 for empty spectrum *and* for an FM station.
  `tetra_burst_find()` finds the burst grid **from the symbols alone**, which
  is worth keeping even though the standard is now to hand: it needs nothing
  transcribed, so it works before anybody knows which technology this is. A burst is instead
  recognised by its shape: some positions repeat every period and the rest do
  not. On air it returns 255 symbols — one TETRA timeslot — in every chunk, at
  0.80 against a runner-up of 0.36, with about 180 of the 255 positions fixed;
  empty spectrum and FM return nothing and their best lag wanders. It takes the
  *fundamental*, since anything with a period of 255 repeats as well at 510 and
  1020, and it needs contiguous symbols — a stream stitched from chunks that
  each began at their own timing phase smears every burst position together.
- `src/tetra_sync.{c,h}` — one layer further, Decoder side: 120 scrambled bits
  → descramble → (120,11) de-interleave → depuncture and Viterbi over a
  16-state rate-1/4 mother code punctured to 2/3 → a (76,60) CRC-CCITT → a
  60-bit SYNC PDU. The same chain at different lengths — (140,124) over 144
  type-2 bits, a (216,101) interleaver — reads the broadcast network channel
  out of block 2 of the same burst, which unlike the synchronization block is
  scrambled with the network's **own** extended colour code and so cannot be
  read until the synchronization block has given it up. On `captures/` it reads
  **MCC 268 (Portugal), MNC 3, colour code 17, location area 4375**, with the
  slot, frame and multiframe counters advancing, on **202 of 202**
  synchronization blocks and 190 of 202 broadcast blocks. MCC 268 is what `gsm_bcch` reads from a different technology on a
  different band. Every constant is transcribed from ETSI EN 300 392-2, and the
  scrambler's seed was one slot out at first — the chain round-tripped
  perfectly anyway, because a wrong scrambling sequence is its own inverse just
  as a right one is. **The parity passing on air is the only check that could
  have caught it**, and it is the only one that establishes any of this.
- `src/fm_dsp.{c,h}` (`fm_`) — FM broadcast: discriminator, a coherent 19 kHz
  pilot, and the RDS subcarrier down to soft symbols. **Every rate in the
  multiplex is a whole multiple of the pilot** — the subcarrier is three times
  it and the symbol rate is it over sixteen — so a station transmits the pilot
  precisely to spare a receiver any blind loop, and there is none here.
  Lock is *coherence*, not amplitude: the pilot's size against the multiplex
  ranks a 48 dB station below a 29 dB one, because the loud one has more audio
  in the denominator.
  `fm_audio_decode()` is the sound, in stereo: low-pass, decimate by a whole
  number so no resampler is needed (2 MS/s over 40 is exactly 50 kHz), 50 µs
  de-emphasis — which is Europe, and nothing in the signal says which — and a
  slow level follower. The difference signal rides at 38 kHz with its carrier
  suppressed, and **38 kHz is exactly twice the pilot**, so the same fact that
  hands over the RDS subcarrier hands this over: no loop, no ambiguity. Both
  channels are the sum until the pilot locks. **Play** opens the device on
  first press.
  A resonator sits in front of the pilot loop, and it is load-bearing: the
  correlator sees the whole multiplex, so a stereo station's own 38 kHz
  subcarrier was dragging the loop it is demodulated from. Taking it out moved
  **stereo separation from 23 dB to 65** — the pilot's phase is doubled to
  reach the subcarrier, so noise on it arrives twice as large. `fm_pilot_ppm`
  is the **transmitter's** pilot offset, not this receiver's clock: five
  stations here spread over 59 ppm while each repeated to one, and a pilot is
  only held to ±2 Hz (±105 ppm at 19 kHz).
  The pilot's lock takes **coherence and presence together** — coherence says
  it is a tone, and on a clean signal carrying no pilot at all the loop finds
  a coherent scrap at 19 kHz and reads 0.74; the pilot's size against the
  multiplex says whether it is there. Neither alone is right, and the size
  alone ranks stations backwards.
- `src/fm_scan.h` — walking band II, in two passes and for an arithmetic
  reason: 205 channels on a 100 kHz raster, and deciding whether one carries
  RDS means demodulating it for a quarter of a second, so visiting all of them
  is a minute to find the fifteen that exist. A receiver at 2 MS/s sees
  1.6 MHz at once, so thirteen tunings say where the carriers *are* and only
  those get the quarter second — eleven seconds against sixty. That asymmetry
  is what makes FM the cheap band to scan and does not hold for the cellular
  ones.
  The FM view decodes baseband in **fixed non-overlapping chunks** and
  accumulates the *bits*, not the baseband: one timing search and one axis
  cost work proportional to their span, so the span stays short, while radio
  text needs twenty-five seconds of groups. A sliding window cannot do it —
  it re-derives its timing offset and drops a leading symbol each pass, so
  which absolute symbol an index means moves underneath you.
- `src/rds.{c,h}` — one layer further, Decoder side: the (26,16) block code,
  the five offset words, groups, and a station's identification, programme
  type and name. **RDS has no preamble**, so synchronisation is a search: a
  syndrome matches by chance about once in two hundred tries, which is why
  four in the offset order is the gate and `rds_sync_odds_per_million()` is
  the number behind it. On `testfiles/fm_rds_tsf.bin` it reads 0x8343, `TSF`,
  news.

A technology DSP module exposes the operations its standard needs and reuses
the generic core where those primitives fit (ADR-0023); modules share dependency
and testability boundaries, not a uniform interface.

Two hard constraints on this layer:
- **No external DSP library** — the FFT and estimators are deliberately
  hand-written and self-contained (ADR-0003). Do not introduce FFTW/liquid-dsp;
  see `docs/liquid-dsp-sdrprobe-assessment.md` for the assessment behind it.
- **The DSP never links the GUI.** The checks link `-lm` only, no raylib and no
  librtlsdr. Anything you add to a `*_dsp.c` must keep that true.

### Presentation: sdrgui components over vendored raygui

- `src/sdrgui.h` with `sdrgui_plot.c`, `sdrgui_scope.c`, `sdrgui_decode.c`,
  `sdrgui_widgets.c` (`sdrgui_`) — reusable visual components. They take plain
  data and geometry and **never see `struct app`** (ADR-0007), and depend only
  on raylib. Every chart draws inside the rect it is handed, reserving its own
  caption strip and label gutter via `sdrgui_chart_area()` — a caller cannot
  compute that clearance, because label width depends on the values.
- `vendor/raygui.h` + `src/raygui_impl.c` — pinned immediate-mode widgets,
  expanded in one isolated TU compiled with `-w` because the header is not
  `-Wall -W` clean. Keep it that way.
- The rendering seam carries **raw centred complex I/Q**, not a pre-reduced
  magnitude frame — that is what lets the spectrum and scatter views exist
  (ADR-0005).

### Application

`src/app.h` names what is genuinely shared, and `src/options.c` parses the
command line. State that belongs to one area lives with it: `struct
acquisition` in `acquisition.h`, and `struct scope_view`, `struct gsm_view`,
`struct calibration`, `struct settings_panel` and `struct adsb_view` in
`app.h`. Reach for `app->cal.*` rather than adding a `calibration_*` field back
to `struct app` — advice `struct app` did not follow until sixteen of them
were counted and moved, which is what the audit in
`.scratch/deepening/issues/06-*` was for: **85 fields when it was counted, 37
now, and the ticket is closed on that number.** Twenty are containers, eight
are genuine handoffs, three are the receiver's applied state (ticket 09,
parked for the second receiver), and six are `sdrprobe.c`'s own process
lifecycle -- read by that one file, which is the deletion test failing in the
harmless direction: a field read once by its *owner* is untidy where one read
once by somebody else is misplaced, and only the second was ever the fault.
**The two largest reductions came from tickets that were not carve-outs at
all** -- `struct signal_frame` took about twenty loose arrays and counters,
`struct receiver_applied` took three -- because they asked what *owns* a field
rather than where it should live. Three earlier moves corrected the field's
*owner* rather than its address, and **the question that found all three is
worth asking of every field before moving it**: `scan_selected_arfcn` was the
GSM view's inspected channel and not the scan's, set by `--arfcn` with no scan
involved; `scan_step_count` was a copy of `bandscan.plan.step_count` that
needed deleting rather than moving; and `settings_error` was also the
acquisition layer's failure line, which is what left `receiver_error` stale on
the very path it had just been created for. If the frame loop needs something
from a view, give the view an entry point rather than reaching into its fields
—
`view_scope_resize_if_needed()` is the pattern. **`struct survey_view` is the
one that has been split rather than merely moved**: what decides lives in
`struct survey_session` and what draws lives beside it, so everything left in
`survey_view` is a text field, a menu, a frequency window, a selection or a
lease token. Each screen has a file — `view_scope.c` (the four Scope views),
`view_gsm.c`, `view_adsb.c`, `view_lte.c`, `view_tetra.c`,
`overlay_calibration.c`,
`overlay_settings.c` —
with `src/view.h` declaring what they share. `src/sdrprobe.c` is down to
acquisition, the tab/header chrome, the frame loop and `main`.

Be clear on what that split is and isn't: every view still reads one big
`struct app`, so this is an organisation of the same coupling, not a set of
modules. Giving acquisition and each view their own state is the change that
would make them modules; `app.h`'s header comment says so.

Its shape:

- **Gain is the profile's, and a list and a range are the same widget.**
  `device_gain_option_count()` / `_value()` / `_format()` turn a tuner's 29
  discrete steps and an AD9361's continuous range into one stepper, because
  that is what a panel shows either way. **`GAIN_UNIT_INDEX` exists because an
  AD9361's receive gain is a gain-table index** that UHD advertises as
  `0..76` and looks like dB -- what a step is worth depends on which of three
  band tables is loaded, chosen at 1300 and 4000 MHz -- so the panel writes
  `index 40` rather than a decibel it did not measure.
- **A retune is a transaction, and it is checked.**
  `src/receiver_runtime.{c,h}` owns the sequence every screen's retune goes
  through -- stop, apply, flush, read back, restart -- and the rollback at
  each step, and `check-receiver-runtime` drives all of it against a fake
  device and a fake worker. **Not one of those branches had ever executed
  under a check**: they took `struct app`, they lived beside `main()`, and the
  receiver path is the half no check reaches (ADR-0012). The case that matters
  is the rate taking and the tuning then refusing, where a refusal has to put
  the *rate* back too or the receiver is left sampling at a rate nothing asked
  for. Phase 1a of that ticket also measured something worth knowing: **the
  RTL-SDR backend does not refuse an unreachable setting** -- 10 Hz and a rate
  inside librtlsdr's own hole both return success and read back -- so
  "Receiver rejected ..." is a message for a failure this device does not
  produce from an out-of-range value, and nothing notices a tuning the tuner
  could not honour.
- **Why a retune failed has its own name.** `retune_receiver()` is the retune
  every screen uses, and all five of its failure messages used to be written
  into `calibration_status` and prefixed "Calibration" -- so a survey step
  that would not tune reported its reason on the calibration overlay's status
  line, on a screen nobody was on, about something that was not calibration.
  `view_lte.c` read that buffer by hand to find out why 1.92 MS/s had been
  refused, which is the tell. It is `app->receiver_error` now: one writer,
  and whichever screen asked is the reader. Two calibration paths had been
  *depending* on the shared buffer -- returning -1 with no status and letting
  the headless report print whatever the retune had left there -- and quote it
  deliberately now. **The acquisition lifecycle was the other half of the same
  buffer**, writing into `settings_error` instead: `start_acquisition()` and
  `stop_acquisition()` are what `retune_receiver()` calls, so fixing only the
  retune left `receiver_error` stale on exactly the path it was made for. Both
  halves report there now, and the one path that returned -1 with no message
  at all -- `acquisition_attach_source()` -- is why the headless line had an
  `"unknown"` fallback.
- **The receiver is behind a seam.** `src/device_backend.h` is a vtable --
  open, close, tune, rate, ppm, gain, flush, stream, stop -- and
  `<rtl-sdr.h>` is included by **exactly one file**, `backend_rtlsdr.c`.
  `struct app` carries a `struct device_session`, not an `rtlsdr_dev_t *`.
  There are two implementations plus a capture (`backend_capture.c`, which is
  mostly refusals because a recording holds one tuning at one rate with one
  gain baked in), and `check-device-backend` adds a fake and a
  nothing-implemented backend so the failure and NULL paths are reachable
  without hardware. `flush` is an entry rather than a detail:
  `rtlsdr_reset_buffer()` had thirteen call sites and a backend with no
  pipeline returns 0, which is truthful and lets every caller keep one
  retune-then-flush path. **UHD is optional** -- `HAVE_UHD` defaults to 0
  because the adapter is unwritten, and `device_backend_uhd()` returns NULL in
  a build without it, so callers ask rather than testing a macro.
- **Threading** lives in `src/acquisition.c`, which owns `struct acquisition`
  and does not include `app.h`. A worker (`receiver_worker` for the librtlsdr
  async callback, `file_worker` for the paced file pacer) hands 256 KB blocks
  to the render thread through a **single mutex-guarded, overwriteable slot** —
  `struct latest_block`, consumed by `consume_latest`. Not a queue: a slow
  renderer drops blocks rather than lagging (ADR-0002). Headless *file*
  playback is the one exception: `acquisition_set_lossless()` makes the file
  worker wait for the consumer instead of overwriting, and stop pacing to real
  time, so a scripted decode sees every block and gives the same answer twice.
  Never set it for a receiver — blocking the librtlsdr callback loses samples
  for real. SIGINT/SIGTERM are
  blocked around `pthread_create` so only the main thread handles them.
  The device handle, playback file and sample rate are *borrowed*: call
  `acquisition_attach_source()` before starting a worker, or it reads a NULL
  capture and segfaults — the fields exist and zero-initialise, so the
  compiler will not tell you.
- **Frame loop** (`run_gui`). Each frame runs an input phase then a draw phase.
  Input is a fixed if/else precedence chain: settings overlay → calibration
  overlay (with scan) → tab switch → settings/calibration buttons → per-tab input.
  Calibration and settings are full-screen overlays orthogonal to the tabs
  (ADR-0008); `enum active_tab` (Scope/Decode) and `enum decode_kind` replaced the
  old ad-hoc mode flags — don't add a new one, extend those enums.
`--survey-save` writes a scripted sweep to `surveys/` and folds it into the
site's history, which is what the window's Save button does -- the last
decision in that view that needed a person.

Walking the LTE chain over a live cell, which `probe-lte-chain` only does for
a capture:

```sh
./sdrprobe --headless --lte-chain --earfcn 6200 --lte-chain-seconds 30
./sdrprobe --headless --lte-chain --lte-chain-band 20     # scan, walk the best
```

Four lines per block -- PSS, SSS, power, MIB -- a `neighbour` line for any
other cell on the carrier, then a funnel and **one verdict per identity**.

The verdict is the part that matters, and it is not a count. A carrier holds
more than one cell -- EARFCN 3625 here holds two -- but the multi-cell search
also mistakes sidelobes for cells, and it makes the same mistake every block,
so a false identity repeats as faithfully as a true one. On that carrier PCI
410 was reported 59 times in 364 blocks and never decoded anything, while
PCI 190 was reported 104 times and read 16 messages. Any threshold on
sightings would have confirmed the wrong one. `src/lte_confirm.h` asks instead
whether the identity's *own* broadcast channel decoded -- scrambled with the
identity, checked by a CRC, and so not something repetition can manufacture --
and reports `confirmed`, `unread` or `spurious`. Three verdicts, because "seen
often and never read" is its own answer and a weak real cell lands there too.

**The antenna-port coherence gate that suppresses those false identities is a
gate on the *neighbours*, not on the cell the carrier is about**, and it was
applied to both until 2026-09-09. `lte_cell_search` admits the strongest
root's cell on the primary and secondary sequences alone -- no coherence
anywhere -- and every other caller takes that answer, so gating it again here
made `lte_cell_search_all` able to report **fewer** cells than the path it
generalises: `lte_b8_pci330_4port.bin` came back empty on 1 of its 6 blocks
where the single-cell search returns cell 330, because two co-channel cells
depress each other's reference coherence under 0.55 and both were dropped.
`check-lte-dsp` pins the invariant on both real captures now -- whatever the
single-cell search finds, the multi-cell search finds too -- and on air the
second cell of EARFCN 3625 went from 57 decoded broadcast messages in 45 s to
186, still `confirmed`. Nothing was loosened for it: the constant is
untouched and the neighbours still have to earn their place. The `power`
line is 36.214's reference-signal measurements over the six central resource
blocks: `rsrp_dbfs`, `rssi_dbfs` and `rsrq_db`. **RSRP is dBFS and not dBm**,
because nothing here knows the antenna's gain or the cable's loss, so it
compares cells on this receiver at this gain and nowhere else; RSRQ is a ratio
of two powers through the same chain, so every fixed gain cancels and it is
directly comparable with a handset's. `check-lte-dsp` pins exactly that by
doubling a buffer and asserting RSRP moves 6.02 dB while RSRQ does not move at
all. It is also what tells two cells on one channel apart: EARFCN 3625 here
carries PCI 190 at -33.3 dBFS and PCI 402 at -35.0. Which stage stops is
the diagnosis: no PSS is tuning or band, PSS without SSS was the conjugated
sequence, SSS without parity is the broadcast channel, and parity without a
repeat is chance. A live cell gave 175 blocks, 169 cells, 168 messages.

Calibrating with no window, which is how the gate is reachable at all
(ADR-0012):

```sh
./sdrprobe --headless --calibrate gsm --arfcn 113
./sdrprobe --headless --calibrate lte --earfcn 6200
./sdrprobe --headless --calibrate lte --calibrate-band 20   # scan, take the best
```

One `cal-measure` line per residual and a `calibrate-result` at the end saying
whether it locked and, if not, which clause of the gate was still unsatisfied.
Every measurement rather than a summary, because the verdict is one bit and the
sequence is what shows whether the scatter is the estimator or the crystal.

Calibration takes two references. GSM measures an FCCH tone; LTE takes the
offset `lte_cell_search` already measures -- coarsely from the primary
sequence, then the whole subcarriers by search, which is the half that matters
because an uncalibrated dongle is two subcarriers out at 800 MHz. Both feed the
same gate through a third source, `CALIBRATION_SOURCE_LTE`, and the source rule
matters more with three than with two: residuals from different references have
different centres and a buffer holding both passes the gate while suggesting a
correction belonging to neither. An LTE calibration borrows 1.92 MS/s
(ADR-0014) and gives the rate back on close. Measured on air, the two agree to
about a ppm -- GSM ARFCN 113 gave -31.3 and LTE EARFCN 6200 gave -32.5.

- **Calibration lock** (`update_calibration_measurement`, `robust_center_spread`)
  gates on a median/MAD-based standard error over a *source-homogeneous* residual
  buffer — mixing centroid and FCCH residuals is the bug the gate exists to
  prevent (ADR-0004). Do not soften the gate without reading it.

## Versioning

`src/version.h` holds three numbers; the window's corner and `--version` are
both built from them, so they cannot disagree. **Semantic Versioning 2.0.0,
read against the command line, the headless reports and the file formats --
not against the screens** (ADR-0016). A moved panel is MINOR; a decode
corrected to read a field it previously got wrong is PATCH, because the wrong
answer was never the contract.

Bumping it is editing three numbers in that header. Nothing derives it from
git: a build from a dirty tree would claim to be a tag it is not.

**And every `check-*` rule belongs in `CHECK_UNITS`.** `check-signal-probe`
did not, so the suite existed, passed, was picked up by `check-touched` --
which reads the rules rather than the list -- and was never run by the gate or
by the pre-push hook. The two failure modes are the same shape and neither is
visible from a green run: the audit is one line and belongs beside the header
one.

```sh
for r in $(grep -oE '^check-[a-z0-9-]+:' Makefile | tr -d ':' | sort -u); do \
    case "$r" in check|check-dsp|check-touched|check-pipelines) continue;; esac; \
    grep -q "$r\b" <(sed -n '/^CHECK_UNITS=/,/^$/p' Makefile) || echo "NOT GATED: $r"; \
done
```

**Every header the program includes belongs in `APP_HDR`**, and nine did not.
Five of them -- `chart_window.h`, `help_layout.h`, `scan_layout.h`,
`scope_layout.h`, `settings_layout.h` -- were never listed at all, so the
fault is older than the four below and not confined to one session. The audit
is one line and worth re-running after adding a header:

```sh
for h in $(ls src/*.h | xargs -n1 basename); do \
    grep -q "SRC)/$h" Makefile || echo "MISSING: $h"; done
```

The four added in one afternoon were:
`panel_rows.h`, `lte_stats.h`, `lte_confirm.h` and `lte_findings.h` were each
listed only by their own `check-*` rule, so editing one rebuilt its check and
not the binary. That is how a screenshot came back showing wording that had
already been changed, and `make check` cannot catch it because the checks have
their own dependency lists and are perfectly up to date.

`version.h` is in `APP_HDR` so that editing it rebuilds. It was not, for a
while, and the failure is quiet in the worst way: the header says one version,
`make` reports nothing to do, and the binary keeps claiming the last one it was
built with. The corner and `--version` still agree with each other -- they
share the header -- which is exactly what makes it hard to notice.

## Conventions

- C, `-Wall -W` clean, 4-space indent, 80-ish column wrap, `/* ... */` comments.
  Prefixes are namespaces: `sdr_dsp_`, `gsm_`, `adsb_`, `sdrgui_`.
- Functions return `0`/negative for success/failure and print to `stderr`;
  `struct app` is threaded through explicitly, no globals beyond the signal flag.
- Signal conventions match dump1090 and are not free parameters: unsigned 8-bit
  interleaved I/Q with 127.5 = zero, 2 MS/s (1 sample = 0.5 µs), block size
  `16*16384`.
- Test captures are `testfiles/<tech>_<detail>.bin`, each with a `.json`
  sidecar. `lte_b20_pci28.bin` is at **1.92 MS/s**, not the house rate, and
  must keep reading **cell 28** under the normal cyclic prefix in **29 of its
  30 blocks** — that identity is the check a conjugated PSS cannot pass. Both
  those numbers were wrong here until `check-lte-session` measured them: this
  said "cell 32", which is not what the capture reads and not what its own
  filename says, and "in every block", which overstates it. **Block 25 reads a
  primary sequence at 0.80 and no secondary one at all** — the "PSS without
  SSS" case named further up as its own diagnosis. It is pre-existing and
  `check-lte-session` pins it exactly, because 30 would mean something improved
  and 28 that something regressed. `adsb_cpr_pair.bin` is the only one recorded by the app itself
  (`"provenance": "recorded by sdrprobe"`, 29.7 dB, R820T); it must keep
  decoding 6 frames with 1 global CPR position resolved, which is what makes it
  worth keeping — it is the only capture that exercises the even/odd pairing
  cache end to end. `adsb_modes1.bin` is denser but its provenance is unknown. The three GSM captures must
  keep decoding their own BSIC in `check-gsm-dsp` — 59 (NCC 7 / BCC 3) for
  `gsm_arfcn_69.bin`, 56 (NCC 7 / BCC 0) for `gsm_arfcn_73.bin`, 38 (NCC 4 /
  BCC 6) for `gsm_arfcn_113.bin` — with frame
  numbers that increase and track the burst timeline. The three BCCs are the
  point of having three: the BCC picks the training sequence every normal
  burst is found by, so hardcoding one passes ARFCN 69 and fails 113.
  `tetra_cc17.bin` and `tetra_cc32.bin` are a pair and the pair is the point:
  colour code 17 against 32, location area 4375 against 4658. The broadcast
  channel is scrambled with the network's **own** colour code, read out of the
  synchronization block first, so a decoder that hardcoded one would read one
  capture and fail the other — the same argument that gives the GSM set three
  captures for three BCCs. Half a second each at 2 MS/s, which is ample because
  this base station sends a synchronization burst in every timeslot, about
  seventy a second. `tetra_cc32.bin` is tuned to 392.8735 rather than a round
  number: recorded at 392.84 the carrier was 33.5 kHz away, the coarse
  estimator pinned at the edge of its range and half the blocks failed — and
  since TETRA channels are 25 kHz apart, a search wide enough to cover that is
  wide enough to select the neighbour.
  `carrier_75000_bare.bin` is not a technology and that is the point: 2 s of
  the 75.0005 MHz clock artifact -- the first member of a 75 MHz x 2^n family
  clocked with this receiver, recorded here as 25 MHz x 3 until 175 and 225
  were measured absent (`.scratch/device-model/issues/10-*`) -- which the band
  plan calls an
  ILS marker beacon and which carries nothing. Recorded **300 kHz below it**,
  so the carrier lands at +300 kHz and clear of the receiver's own DC offset;
  `signal_find_carrier()` guards a band around zero and would otherwise find
  the DC spike. It must keep reading a bare carrier at about 47 dB over its
  floor with about 0.92 of the channel standing still, and no burst structure.
  It is the only real-signal check `signal_probe` has -- everything else there
  is synthetic, and a synthetic signal agrees with whatever assumption built
  it. Windowed on 140-210 kHz it returns a real neighbour at +176 kHz at
  38 dB, which is what makes the search window the caller's responsibility
  rather than a default. **Searched across the whole span it returns the
  target**, because that is the strongest line there; it used to return the
  neighbour, and that was the coarse scan's blind comb rather than a fact
  about the capture -- see `SIGNAL_COARSE_PAIRS`.
  `fm_rds_tsf.bin` is at **2.048 MS/s**, tuned to 89.5 where TSF is, and
  three seconds long. It must keep reading identification 0x8343 and the name
  `TSF`; the name alone would pass with the differential sense backwards, so
  `check-pipelines` asserts the programme type as well, which lives in a
  different block of every group -- and the tuning, because the capture it
  replaced was tuned 89.6 against a station at 89.5 and nothing said so.
  **Three seconds rather than two is margin, not generosity**: a name is four
  segments seen whole twice and agreeing, one second of this capture names
  nothing, and two seconds names it only depending on where the segment cycle
  falls -- a separate two-second recording of the same station minutes earlier
  did not. The old capture was on the lucky side of that.
  `gsm_arfcn_113.bin` is the only one whose cell is still on air — 69 and 73
  went off the air with the operator's refarming, so they are historical and
  cannot be re-recorded. Those real-signal
  invariants are the only checks a wrong SCH field layout cannot satisfy: the
  synthetic round trip passes against any layout the encoder shares.

## Working in this repo

- **When the context has grown long, assess the skills before continuing.** A
  long session is the only time there is evidence to assess them with: by then
  it is on record which skill was reached for, which was ignored, and what had
  to be worked out from scratch anyway. Three questions, each answered by a
  change to `.claude/skills/` rather than a note: **did a skill earn its
  place** -- one that was loaded and not followed, or whose advice had to be
  worked around, costs context on every invocation and is trusted, so improve
  it or delete it; **was the same script written more than once** -- a scratch
  harness written three times is a tool, and belongs in `scripts/` behind a
  `make` target where the numbers land in a ticket instead of a transcript,
  the test being repetition rather than usefulness; and **was something
  learned that no skill knows** -- the failures worth writing down are the
  ones where the arithmetic was right and the claim was false, and the skill
  that should have prevented it is the one to amend. **Say what changed**: a
  skill is instructions that will be followed without being re-read, so a
  silent edit to one silently changes how this repository is worked on. Name
  the skill, the change, and the part of the session that was the evidence.
  `AGENTS.md` has the long form.
- **Vocabulary is enforced by `CONTEXT.md`.** Each term lists an _Avoid_ line
  (e.g. the scatter view is never a "constellation" in Probe language, "sample
  block" is never a "packet"). Use the glossary's term in code, comments, UI text,
  and commit messages; flag a genuine gap rather than inventing a synonym.
- **Decisions live in `docs/adr/`.** Read the ADRs touching your area first. If a
  change contradicts one, say so explicitly ("Contradicts ADR-0007, but worth
  reopening because…") instead of silently overriding it.
- **Issues are local markdown** under `.scratch/<feature-slug>/`: `spec.md` plus
  one file per ticket at `issues/NN-<slug>.md`, with a `Status:` line
  (`needs-triage` / `needs-info` / `ready-for-agent` / `ready-for-human` /
  `wontfix`) and conversation appended under `## Comments`. See
  `docs/agents/issue-tracker.md`.
- Deep-dive references: `docs/ARCHITECTURE.md` (this program's layers and state),
  `docs/dump1090-reference.md` (dump1090 internals — that source is *not* in
  this repo), `docs/cellular-frequency-correction.md`,
  `docs/sch-frame-number-decode.md`, `docs/sdrprobe-implementation.md`.
