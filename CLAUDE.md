# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` carries the exhaustive per-file and per-view tour (every CLI flag, key,
button, and file). This file covers the build/test loop, the big-picture
architecture, and the invariants that are easy to break. Read `AGENTS.md` when you
need the detail.

## Build & test

```sh
make                  # build ./sdrprobe (needs librtlsdr + raylib dev headers, pkg-config)
make check            # everything below, ~18 s, no window and no receiver
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

`make check` is the one to run before claiming a change is sound, and
`make hooks` makes git run it for you: it points `core.hooksPath` at
`scripts/hooks/`, whose `pre-push` refuses a push whose tree does not pass.
`git push --no-verify` is the escape hatch. **ADR-0012
governs what belongs in it: every decision the program makes must be reachable
by a check that needs no window, no receiver, and no person — drawing is
exempt, deciding is not.** The rule that keeps that true is that a function
which draws or reads input may not also decide; if it computes a threshold,
chooses a range, maps a pointer to an index, or advances a state machine, that
part comes out into a unit with a name. Logic not yet reachable is listed in
`.scratch/testability/`, one ticket per area — add to it rather than leaving a
gap implicit.

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

The site is a combo: type a new one, or pick one this receiver has been to
before, from the list `config_remember_site()` keeps -- spelling one place two
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
- **Calibration lock** (`update_calibration_measurement`, `robust_center_spread`)
  gates on a median/MAD-based standard error over a *source-homogeneous* residual
  buffer — mixing centroid and FCCH residuals is the bug the gate exists to
  prevent (ADR-0004). Do not soften the gate without reading it.

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
