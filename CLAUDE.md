# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` carries the exhaustive per-file and per-view tour (every CLI flag, key,
button, and file). This file covers the build/test loop, the big-picture
architecture, and the invariants that are easy to break. Read `AGENTS.md` when you
need the detail.

## Build & test

```sh
make                  # build ./sdrprobe (needs librtlsdr + raylib dev headers, pkg-config)
make check            # everything below, ~55 s, no window and no receiver
make check-touched    # only the suites covering what git says changed
make check-dsp        # the four DSP checks below
make check-sdr-dsp    # one check in isolation — generic core
make check-gsm-dsp    # GSM plugin (+ the core it reuses)
make check-adsb-dsp   # Mode S / ADS-B plugin
make check-band-plan  # the frequency allocation table
make check-options    # the command line: every flag, value, and rejection
make check-survey     # the survey window's zoom, pan and clamp arithmetic
make check-survey-sweep # the sweep's step plan, fold, and measurement
make check-suspect    # candidates that look like the receiver, not the band
make check-calibration # the lock gate, and the machine that fills its buffer
make check-scan       # the band scan's coverage and the channel it chooses
make check-adsb-analysis # trace latching, the message log, the funnel
make check-gsm-continuity # whether consecutive SCH decodes hang together
make check-gsm-bcch   # four bursts to a System Information message
make check-acquisition # the block slot, both its modes, and its shutdown
make check-layout     # GSM view geometry (raylib headers only, no window)
make check-geometry   # where a chart's plot sits, and which bar is under the pointer
make check-input      # which control a key press reaches
make check-pipelines  # the built program over testfiles/, asserting on stdout
make hooks            # run `make check` on every git push (once, per clone)
make clean
```

**Run the suite that covers the change, not all of them.** One suite is under
a second and the full set is the better part of a minute, so `make check` after
every edit turns a fast loop into a slow one. The loop is three sizes:
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
agrees with it.

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

White-box diagnostics (not tests — they print a walk through a decode chain and
compile the plugin's `.c` in to reach its statics):

```sh
make probe-gsm-chain                        # defaults to testfiles/gsm_arfcn_69.bin
make probe-gsm-chain FILE=captures/x.bin
make probe-adsb-chain FILE_ADSB=testfiles/adsb_modes1.bin
make probe-lte-chain FILE_LTE=testfiles/lte_b20_pci28.bin
make probe-periodicity FILE_PERIODICITY=captures/x.bin   # LTE or NR? which grid?
```

`probe-periodicity` is the odd one out: it demodulates nothing, and works on a
signal no plugin here understands. Two lag correlations -- a burst folded over
its own period, and the cyclic prefix against itself -- say whether a carrier
is LTE (a burst every 5 ms) or 5G NR (every 20, and none at 5), and whether it
runs at 15 or 30 kHz. It is how band 28 was found to be carrying NR rather than
a weak LTE cell.

What the DSP costs, against the 65.5 ms of signal one block covers:

```sh
make bench-dsp                              # as built, no -march
make bench-dsp BENCH_ARCH=-march=native     # with this machine's SIMD
```

The answer as of this writing: the Scope path uses about 7 ms of the 65.5,
the GSM view about 28, and `-march=native` changes none of it beyond noise.
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
command line and `fft_size` persists it in the config.

`--analysis` opens a decode view on its charts rather than its data — one flag
for all of them, since every decode view has the same two arrangements.
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
./sdrprobe --file testfiles/fm_rds_tsf.bin --sample-rate 2048000 \
    --frequency 89.6M --headless --technology fm --decode --once
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

The site and the antenna are combos over lists the configuration keeps
(`config_remember_site()`, `config_remember_antenna()`), because one place or
one antenna named two ways is two of them and levels only compare within one of
each. The antenna defaults to `telescopic`.

**Band...** beside the range fields fills them in from the band plan rather
than from memory: the 54 allocations this tuner can reach, with the ones that
have a decoder behind them picked out, and a dwell chosen to suit the width —
half a second for anything under about fifty megahertz, down to the default
for the whole tuner. `src/survey_bands.h` is the arithmetic and
`check-survey-bands` asserts that nothing offered is out of the tuner's reach
and nothing reachable is left off.

A sweep's peaks are grouped into signals by `src/survey_carrier.h` before
anything reads them: two maxima are one carrier when the power between them
never drops far below the lower of the two, and each carrier's extent runs to
the trough on either side rather than to a fixed number of decibels down --
which is what gives a weak peak a width that means something. Its `centre_hz`
is the middle of that extent and identifies the signal; `power_centre_hz` says
where the energy sits, and the two part company on a lopsided carrier.

The tuning correction is kept per site (`config_site_ppm()`): it drifts and is
measured against whatever reference a place offers, so arriving somewhere the
receiver has been calibrated restores that calibration rather than the last one
measured anywhere. A sweep's marks are claims from a tenth of a second each;
**Ask again**, or `--survey-confirm` on a scripted sweep, revisits each with six
blocks on the frequency and prints a verdict. The site is a combo: type a new
one, or pick one this receiver has been to before, from the list `config_remember_site()` keeps -- spelling one place two
ways makes it two places and nothing downstream can tell. Saving also folds the
sweep into `surveys/history-<site>.txt` (`src/site_history.c`), which is what
lets the window tick the candidates this site has never heard, mark where
something it knows has gone quiet, and say under the cursor how many sweeps
ago. Matching uses the coarser of the two sweeps' bin widths; the reason is in
`site_entry.bin_hz` and it is not optional.

Both the window and the headless report go through `survey_candidates_from()`
in `src/survey_store.c`, so what a candidate *is* -- where it was found, what
it measured to, whether it resembles the receiver, which allocation it falls in
-- is decided once. It used to be decided inside a `printf` loop, where the two
could have disagreed about the same peak with nothing to say so.

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
statistics — it must never claim to have decoded a message; the **Decoder**
context (`docs/contexts/decoder/CONTEXT.md`) starts where bits become a message.
Tabs are presentation only, not the boundary (ADR-0010).

### DSP: generic core + technology plugins

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
- `src/lte_mib.{c,h}` — one layer further, Decoder side: 480 soft bits →
  descramble (four offsets, since one transmission does not say which quarter
  of the 40 ms period it is) → rate dematch → tail-biting rate-1/3 Viterbi →
  CRC-16 masked by the antenna-port count → a Master Information Block.
  `src/lte_gold.h` holds the length-31 Gold sequence both sides need.
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

A plugin supplies a channel map and a reference-tone detector and reuses the core
for everything else (ADR-0001); a decode stage sits behind the same seam even when
it reuses almost nothing of the core (ADR-0009, true of ADS-B).

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
to `struct app`, and if the frame loop needs something from a view, give the
view an entry point rather than reaching into its fields —
`view_scope_resize_if_needed()` is the pattern. Each screen has a file — `view_scope.c` (the four Scope views),
`view_gsm.c`, `view_adsb.c`, `view_lte.c`, `overlay_calibration.c`,
`overlay_settings.c` —
with `src/view.h` declaring what they share. `src/sdrprobe.c` is down to
acquisition, the tab/header chrome, the frame loop and `main`.

Be clear on what that split is and isn't: every view still reads one big
`struct app`, so this is an organisation of the same coupling, not a set of
modules. Giving acquisition and each view their own state is the change that
would make them modules; `app.h`'s header comment says so.

Its shape:

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

Three lines per block -- PSS, SSS, MIB -- then a funnel. Which stage stops is
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
  must keep reading cell 32 under the normal cyclic prefix in every block —
  that identity is the check a conjugated PSS cannot pass. `adsb_cpr_pair.bin` is the only one recorded by the app itself
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
  `fm_rds_tsf.bin` is at **2.048 MS/s** and must keep reading identification
  0x8343 and the name `TSF`; the name alone would pass with the differential
  sense backwards, so `check-pipelines` asserts the programme type as well,
  which lives in a different block of every group.
  `gsm_arfcn_113.bin` is the only one whose cell is still on air — 69 and 73
  went off the air with the operator's refarming, so they are historical and
  cannot be re-recorded. Those real-signal
  invariants are the only checks a wrong SCH field layout cannot satisfy: the
  synthetic round trip passes against any layout the encoder shares.

## Working in this repo

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
