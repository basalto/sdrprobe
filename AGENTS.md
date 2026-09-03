# AGENTS.md

`sdrprobe` is a raylib SDR visualizer and GSM 900 frequency-calibration probe
for RTL-SDR receivers, modeled after dump1090's `modesInitRTLSDR()` acquisition.
Its DSP is split into a generic core (`src/sdr_dsp.c`/`.h`) and per-technology
plugins (`src/gsm_dsp.c`/`.h` for GSM calibration, `src/lte_dsp.c`/`.h` for
LTE cell search, `src/adsb_dsp.c`/`.h` for
Mode S / ADS-B message decoding), and its UI into an SDR component layer
(`src/sdrgui.c`/`.h`) over vendored raygui widgets (see "Files" below). The
window is organised into two top-level tabs — Scope (the four signal views,
keys 1-4) and Decode (message decoders selected by number keys: 1 ADS-B,
2 GSM band analysis, 3 LTE cell search) — recorded in
`docs/adr/0008-top-level-tab-navigation.md`. Calibration remains a button-driven,
global full-screen overlay orthogonal to the tabs. Message decoding lives in a
second bounded context (see `CONTEXT-MAP.md`). No CI.

## Build & run

- `make` — builds `sdrprobe` via pkg-config; requires librtlsdr and raylib
  development headers installed.
- `make hooks` — point git's `core.hooksPath` at `scripts/hooks/`, so
  `git push` runs `make check` first and refuses a tree that does not pass.
  Once per clone; the hook is version-controlled rather than hidden in
  `.git/hooks`, and `git push --no-verify` skips it. A push that only deletes
  remote branches skips it too: no commit is arriving, so there is no tree to
  have broken.
- `make check` — every check below, in about 18 seconds. Nothing it runs needs
  a window, a receiver, or a person. It prints one line per suite saying what
  the suite covers and how many checks it ran, and a total at the end; the
  compiler commands are hidden, and `make V=1 check` brings them back. This is the command to run before claiming
  a change is sound, and the one to extend when adding a decision: ADR-0012
  says every decision the program makes must be reachable from here, and
  `.scratch/testability/` is the register of what is not yet.
- `make check-dsp` — builds and runs deterministic, hardware-free DSP
  checks; it does not require raylib. It runs the per-technology checks
  `check-sdr-dsp` (generic core),
  `check-gsm-dsp` (GSM plugin), `check-adsb-dsp` (Mode S / ADS-B plugin),
  `check-lte-dsp` (LTE cell search), `check-lte-mib` (the LTE broadcast
  channel) and `check-band-plan` (the frequency allocation table); each can be
  built and run on its own.
- The rest of the unit layer, one suite per module, each a `make check-*` of
  its own and all of them in `make check`:
  `check-survey-sweep` (the sweep's step plan, its fold into the survey array,
  and what measuring a candidate adds up to), `check-suspect` (candidates that
  look like the receiver rather than the band -- its own reference comb, and
  the DC offset at a step centre), `check-scan` (how the GSM
  downlink is covered and which channel the scan hands back),
  `check-adsb-analysis` (whether Mode S could be there, which frame the
  analysis charts describe, the message log, the funnel),
  `check-gsm-continuity` (whether consecutive SCH decodes hang together --
  the hyperframe wrap included), `check-gsm-bcch` (four bursts to a System
  Information message: interleaving, the Fire code, and what the message
  says), `check-acquisition` (the block slot, both its
  modes, and the shutdown of a lossless publisher), `check-geometry` (where a
  chart's plot sits inside it and which bar the pointer is over) and
  `check-input` (which control a key press reaches). Each exists because
  something in that area decided things from inside a file that links raylib;
  `.scratch/testability/` records what each one moved and what a mutation of
  it breaks.
- `make check-options` — the whole command line: every flag, every value
  spelling, every rejection, and the flags that imply others. It is the
  program's other user interface and the only one a script has, so a rejection
  that should happen and does not is a wrong tuning or a capture labelled as
  something it is not. Pure text in, a struct out, so it is also the cheapest
  thing here to check exhaustively.
- `make check-calibration` — the rule that decides whether a frequency
  correction may be applied, and the robust statistics behind it, from
  `src/calibration_gate.h`. Each clause of the gate is refused on its own, and
  the mixed-source hazard ADR-0004 exists to prevent is demonstrated rather
  than described. Worth the attention: a correction accepted too early turns
  the lock green on a wrong answer, and every frequency reported afterwards is
  off by that amount with nothing on screen to say so.
- `make check-pipelines` — `tests/pipelines.sh`, the assembled program driven
  through its command line over the captures in `testfiles/`, asserting on
  stdout. Both GSM captures must decode their own BSIC with frame numbers that
  advance; `adsb_cpr_pair.bin` must decode its frames and resolve a CPR
  position, and must decode *the same number twice* -- headless playback was
  dropping blocks, and until it stopped, every count here was a coin toss.
  Recording must produce a capture and a complete sidecar, and the flags that
  reach those paths must act. Unit checks prove the pieces; this proves they
  are wired together, which by construction they cannot.
- `make check-survey` — the band survey's window arithmetic: zoom, pan, clamp,
  and what pressing Sweep would sweep. Pure doubles in `src/survey_window.h`,
  so it links nothing and opens no window. It exists because those decisions
  could previously only be checked by building an instrumented binary and
  running it against a receiver -- synthetic clicks and keys do not reach the
  raylib window on every desktop -- and two of them shipped wrong: a zoom that
  did nothing before the first sweep, and a Sweep that ignored the region just
  selected. Both are now cases in `tests/survey_window_test.c`. Anything in a
  view that is arithmetic rather than drawing belongs here for the same reason.
- `make bench-dsp` — times each DSP stage against the 65.5 ms of signal a
  256 KB block covers, which is the interval the receiver delivers them at.
  Hardware-free, `-lm` only. `BENCH_ARCH=-march=native` answers "would SIMD
  help" by measuring it rather than arguing about it; as of this writing it
  does not, and `docs/liquid-dsp-sdrprobe-assessment.md` records why and where
  the time would come from instead.
- `make check-layout` — pins the GSM decode view's rectangles and the window chrome at
  several window sizes. Needs raylib's headers for the `Rectangle` type but not the
  library, and opens no window. A failure means geometry moved; if that was
  intended, re-bless the numbers in `tests/layout_test.c` in the same
  commit, so the diff shows what shifted.
- `./sdrprobe [--frequency Hz|K|M|G] [--sample-rate S/s] [--gain max|auto|dB] [--ppm N]`
  plus the scripted flags: `--list-devices` (what is attached and whether it can
  be opened, which is how a busy dongle announces itself), `--device N`,
  `--view NAME` to open on a screen, `--record-seconds N` with `--technology`
  to capture from startup, `--duration N` to quit by itself, and `--headless`
  to acquire with no window at all. `--headless --record-seconds N` is the way
  to make a capture without a display; it prints the path on stdout. Recording
  tees off inside the acquisition thread, so a headless capture is the same
  bytes a windowed one would be -- re-recording a capture through it decodes
  identically. `--arfcn N` tunes a GSM 900 downlink channel the way clicking it
  in the scan chart does (centre 400 kHz below the carrier) and labels a
  recording with the channel and offset; `--gsm-features filter,finecfo,trellis`
  (or `none`) picks the SCH refinements; `--dc-filter on|off` and `--once`
  (play a capture through once instead of looping) round it out. `--headless
  --decode` prints decoded messages to stdout -- SCH lines for GSM, message-log
  rows for ADS-B -- which is how to check a capture from a script:
  `./sdrprobe --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode
  --once` prints BSIC 56, the invariant CLAUDE.md records for that file.
  `--headless --survey` does the same for the band survey, which is otherwise
  reachable only by clicking: with `--file` it surveys the capture's own
  tuning in one step, and with a receiver it sweeps `--survey-range`. It
  prints one `candidate` record per line -- the frequency the sweep found, the
  frequency the measurement refined it to, the occupied bandwidth, any
  suspicion flags, and the band-plan allocation last, being the only field
  that can hold a space. A capture surveyed this way gives the same bytes
  every run, which is what lets an agent diff one survey against another.
  opens magnitude, spectrum, I/Q scatter, waterfall and band-survey views. It needs a real
  RTL-SDR dongle by default; `--file capture.bin` (e.g.
  `testfiles/adsb_modes1.bin`) enables paced, looping hardware-free playback. A
  top-of-window tab bar switches between Scope and Decode. In the Scope tab,
  keys 1/2/3/4/5 switch views, the fifth being the band survey: it sweeps an
  operator-set range (default 24-1766 MHz, the R820T's span) in 1.6 MHz steps,
  charts the power across it, and marks the peaks standing 8 dB above their
  local floor by topographic prominence -- not height above a floor, which
  reports a strong carrier's shoulder as a signal. Selecting a candidate, by
  click or with Up/Down, retunes 300 kHz off it (never onto the DC spike) and
  measures occupied bandwidth, duty and frequency stability over two seconds,
  then names the allocation from `src/band_plan.c` and offers to point a
  decoder at it. The band plan is a lookup and the UI says so: see
  `docs/adr/0015-band-plan-is-a-lookup.md`, which is the boundary this view
  exists next to. The band plan is also
  drawn behind the trace as shaded regions, named where there is room, so a
  peak reads against what the region is allocated to; dragging a rectangle across the
  chart zooms to that span, `+`/`-` zoom, `Left`/`Right` walk the zoomed
  window, `0` returns to the whole sweep, and the wheel zooms over the chart.
  Zoom is read from `GetCharPressed`, not `KEY_EQUAL`/`KEY_MINUS`: raylib names
  keys after US physical positions, and on a Portuguese layout those are
  different keys entirely. The candidate list narrows to the visible window. Zooming re-draws the same measurements rather
  than re-sweeping, and the level axis follows what is on screen.
  A dwell field sets how long each step listens: at the default 0.10 s a step
  sees only what is transmitting at that instant, and raising it lets the
  peak-holding fold catch bursty transmitters, at a cost linear in the sweep
  time. Sweep sweeps whatever the chart is showing: the range in the fields when
  zoomed out, the window when zoomed in -- one button, because the two differ
  only when the view has been narrowed, and typing a range re-anchors the
  window on it so the two can never disagree. Sweeping a zoomed window narrows
  the swept range to it, throwing away what lay outside.
  "Reset zoom" backs out one level for that reason: the zoom first, then the
  whole survey the narrowed sweep replaced (a 44 KB snapshot restores the
  measurements and candidates instantly, rather than re-sweeping for minutes),
  then the tuner's full span in the fields. It never starts a sweep by itself.
  Before the first sweep the view takes its extent from the range fields, so
  zoom, pan and drag work on a freshly opened survey rather than dividing by a
  span of zero. The candidate panel carries "Scan this frequency", which sweeps
  +/-2 MHz around the selection at the current dwell -- the drill-down the
  survey exists for -- and snapshots the survey first so Reset zoom returns.
  "Open waterfall" tunes 300 kHz off the candidate, clears the waterfall
  history and switches to view 4; that tuning is kept rather than restored on
  leaving, which is the one case where the survey does not put back what it
  changed.
  Bin width is the span over 8192, floored at the FFT's own resolution, so a
  narrow sweep is detailed rather than fixed at the old 50 kHz.
  `--survey-range low:high` opens the view and sweeps that
  range without waiting to be asked, and `--survey-dwell` sets the dwell; in the Decode tab, keys 1/2 switch between GSM band
  analysis and ADS-B. The Settings button (or `s`) changes center frequency and, for a live receiver, gain while running; it also toggles the
  default-on spectrum/waterfall DC-spike filter. The HUD reports noise,
  estimated SNR, clipping, and full-scale headroom for gain selection. Up/Down
  manually narrow/widen the active chart's scale. The Decode tab's GSM view
  surveys GSM 900: a channel-power scan (with BCCH/FCCH highlighting) and an
  ARFCN-axis waterfall; it opens on the calibrated channel (or the last selected
  one, otherwise it scans and picks the strongest BCCH), and clicking a channel
  tunes the waterfall above to inspect it. Which channel that pick lands on is
  not stable between sweeps: two scans a minute apart chose BSIC 10 and then
  BSIC 38, both real cells, because which BCCH is strongest moves with
  conditions. A test against live air can assert that *a* cell decodes, never
  which one. On the inspected channel it decodes
  the SCH and prints the BSIC (NCC/BCC) and frame number above the scan chart,
  with a decode constellation of the demodulated SCH symbols beside it (small
  Amp/Derot toggles switch amplitude and differential/derotated views), and a
  Record button saves ~2 s of raw I/Q to `captures/` to build a capture,
  writing a `.json` sidecar beside it with the tuning the samples were taken
  at (centre frequency, sample rate, gain, PPM, tuner, the technology it was
  recorded for, and for GSM the ARFCN and its carrier offset) plus a
  short-block count that is non-zero only when the capture is not contiguous.
  The ADS-B view has the same button; recording is shared
  (`start_capture_record()`), because it is not a property of either decode
  view. The
  Calibration button (or `c`), below Settings, opens button-driven GSM 900
  cellular calibration for ARFCN 1-124 as a full-screen overlay over any tab,
  including expected/measured carrier markers and a stability-gated PPM
  suggestion. The ADS-B view decodes 1090 MHz Mode S extended squitters
  (DF17/18) and shows a newest-first log of decoded messages (time, ICAO, decoded
  fields, and the raw hex frame); it offers a retune-to-1090 affordance on a
  live receiver when tuned elsewhere. A header line reports the decode funnel
  (preambles accepted, squitter-shaped attempts, CRC failures, decoded), which
  is what separates a silent band from frames that are arriving and failing, and
  a View: Analysis toggle adds three charts of the last frame's trace (preamble
  score landscape, pulse-position bit confidence, magnitude envelope) with a
  bit-decision scatter beside the log. The trace latches the most recent
  attempt, pass or fail -- a frame that failed its CRC is the one worth seeing,
  and it prints no ICAO because those bits are not an address. "Hold last good"
  pins the last CRC-valid frame instead. A top-right
  circle shows calibration health (grey uncalibrated, green FCCH-backed lock,
  amber checking, red drift); the optional Settings "Auto GSM drift check"
  periodically retunes to the calibrated ARFCN to re-verify and warns on drift
  (see `docs/adr/0006-gsm-drift-indicator.md`). `h` opens the help overlay from
  any view and over the calibration and scan overlays (not over Settings, whose
  fields are taking typed input): eleven topics on what each chart plots and how
  to read it, opening on the topic for the screen underneath, with Left/Right to
  change topic and Up/Down or the wheel to scroll. Quit with `q`, Esc, or Ctrl-C.
- Built binaries and the DSP check executables are gitignored (not tracked);
  `make` writes them to the repo root and `make clean` removes them.

## Signal conventions (match dump1090)

- 1090 MHz (ADS-B), 2 MS/s (1 sample = 0.5 µs), the supported manual gain
  nearest 30 dB.
- Samples are unsigned 8-bit interleaved I/Q; 127/127.5 = zero signal.
- Block size is `16*16384` = 256 KB, deliberately matching dump1090's block.

## Files

C sources and headers live in `src/`; hardware-free DSP test sources live in
`tests/`. Built binaries are written to the repo root.

- `src/calibration_gate.h` — when a frequency correction may be trusted:
  median/MAD statistics, the standard error the gate reads, and every clause of
  the lock condition. Header-only plain doubles, deliberately outside
  `overlay_calibration.c` so it can be checked without raylib. Read ADR-0004
  before making any clause easier to pass.
- `src/sdr_dsp.h` / `src/sdr_dsp.c` — generic, technology-independent SDR DSP
  core: byte→float I/Q conversion, DC removal, magnitude peak binning, signal
  stats, Hann-windowed complex FFT / dBFS spectra, power-centroid carrier
  estimate, evenly-spaced channel-power reducer, and PPM correction. Prefix
  `sdr_dsp_`.
- `src/gsm_dsp.h` / `src/gsm_dsp.c` — GSM 900 technology plugin: ARFCN→frequency
  map, the FCCH reference-tone detector, and the SCH (Synchronisation Channel)
  decoder — differential-GMSK demod, extended-training-sequence sync, rate-1/2
  Viterbi, parity, and BSIC (NCC/BCC) + reduced-frame-number parse. Reuses the
  generic core for everything else. Prefix `gsm_`. The per-technology DSP split
  (a generic core plus reference-tone plugins) is recorded in
  `docs/adr/0001-technology-plugin-dsp-architecture.md`.
- `src/adsb_dsp.h` / `src/adsb_dsp.c` — Mode S / ADS-B technology plugin (the
  Decoder context): magnitude-domain preamble detection, pulse-position bit
  demod, CRC-24 validation, DF17/18 field parsing (ICAO, callsign, altitude,
  velocity), and CPR global position decode with a minimal even/odd pairing
  cache. Prefix `adsb_`. It reuses only the core's per-pair magnitude, not the
  FFT/centroid primitives; recorded in `docs/adr/0009-mode-s-decode-plugin.md`.
- `src/lte_dsp.h` / `src/lte_dsp.c` — LTE (E-UTRA) technology plugin:
  EARFCN→frequency map, primary-synchronisation-signal correlation against the
  three Zadoff-Chu roots, secondary-sequence detection over the 336 candidates,
  physical cell identity, cyclic-prefix length, frame boundary, and a frequency
  offset measured coarsely from PSS and then refined against the reference
  signals of slot 1. It also extracts the broadcast channel's soft bits, with
  the space-frequency block code undone for one, two or four antenna ports.
  Prefix `lte_`. **It runs at 1.92 MS/s and refuses any other rate**
  (`docs/adr/0014-lte-runs-on-lte-s-sample-grid.md`).

  Two things in it are load-bearing and easy to break silently:

  1. **The frequency offset is measured modulo one subcarrier.** The primary
     sequence reads it from a phase, and a phase wraps: 15 kHz is one full
     turn. An uncalibrated dongle is two subcarriers out at 800 MHz, the
     correlation still locks at 0.8 with all of it present, and everything
     after reads the wrong subcarriers and returns a *confident wrong
     identity*. `lte_cell_search` sweeps integer offsets against the secondary
     sequence to find the rest, then re-finds the peak with the offset removed
     -- a frequency error moves a Zadoff-Chu correlation, not just weakens it.
     This cost a day; see
     `.scratch/lte-cell-search/issues/05-the-broadcast-channel-on-air.md`.
  2. **The sign of the PSS exponent.** Conjugating a punctured length-63
     Zadoff-Chu sequence maps root u to 63 − u, and LTE's roots are 25, 29 and
     34 — so a conjugated generator still finds every cell, sharply and with a
     coherent channel, while swapping N_ID_2 1 with 2 and hiding N_ID_2 0
     entirely. A synthetic round trip cannot see it, because the generator and
     the detector conjugate together. This actually shipped and was found only
     against live air; see
     `.scratch/lte-cell-search/issues/04-the-conjugated-primary-sequence.md`.
  3. **The secondary sequence is detected differentially**, each subcarrier
     times the conjugate of its neighbour. Dividing out a channel measured from
     the primary sequence is the obvious alternative, works perfectly on a
     synthesised frame, and scores 0.44 — indistinguishable from noise — on
     live captures that the differential method reads at 0.75.
- `src/lte_scan.h` — the LTE band scan's order, header-only and checked by
  `tests/lte_scan_test.c`. Every channel of a band named exactly once, whole
  megahertz first. It is a separate file because the constraint behind it is
  not obvious: LTE cannot be swept ten channels to a tuning the way GSM is,
  because the primary sequence is found by a time-domain correlation that a
  frequency error of more than a few kilohertz destroys.
- `src/lte_mib.h` / `src/lte_mib.c` — the LTE Master Information Block, the
  Decoder context's side of LTE and the analogue of `gsm_bcch.c`: 480 soft bits
  in, a message out. Descrambling against four candidate offsets (one
  transmission does not say which quarter of the 40 ms period it is), rate
  dematching, a tail-biting rate-1/3 K=7 Viterbi over all 64 closing states,
  and a CRC-16 whose mask names the antenna-port count. `src/lte_gold.h` holds
  the length-31 Gold sequence both sides of the split need, as a header so
  neither has to depend on the other.
- `src/acquisition.{c,h}` — the worker thread, the single overwriteable block
  slot it hands samples through (ADR-0002), and raw-I/Q recording. Owns its
  own state and knows nothing of `struct app`: the device handle, playback
  file and sample rate are handed to it by `acquisition_attach_source()`
  before a worker starts.
- `src/app.h` — the shared application state (`struct app`) plus the constants
  and enums that go with it. Naming it here is what lets the views live in
  their own files; it is a shared record, not an interface (see its header
  comment).
- `src/options.{c,h}` — command-line parsing and the text parsers behind it.
  Touches no application state, and the settings and calibration panels reuse
  `parse_int` / `parse_frequency` so typed input is parsed the same way
  everywhere.
- `src/view.h` — what the screens share with each other and with
  `sdrprobe.c`: a few widgets, the acquisition lifecycle, and each screen's
  entry points.
- `src/view_scope.c` — the Scope tab's four signal views, plus the waterfall
  texture and scatter history they keep between frames.
- `src/view_gsm.c`, `src/view_adsb.c`, `src/view_lte.c` — the Decode tab's
  three screens. The LTE one also owns the band scan, and is the only view
  that changes the sample rate — it borrows the receiver at 1.92 MS/s and
  gives the rate and the tuning back on the way out, the way the GSM view
  borrows the tuning alone. Its signal mode is two panels read together: what the
  synchronisation signals found, and what the broadcast said. A cell identity
  with an empty panel beside it is a carrier that is present and too weak to
  read, which one panel alone could not tell from an empty band.
- `src/overlay_calibration.c` — GSM 900 calibration and the periodic drift
  re-check, which is a calibration re-run.
- `src/overlay_scan.c` — the band scan that picks a calibration reference. It
  shares no state with calibration; choosing a channel goes through
  `calibration_select_channel()`.
- `src/overlay_settings.c` — the Settings panel and the two buttons that open
  it and the calibration overlay.
- `src/overlay_help.c` — the help overlay and the text in it. The prose lives
  in one table here rather than beside each view, because a reader arrives with
  a question about a chart and wants the neighbouring answers next to it. Every
  figure quoted in that text (window sizes, thresholds, decay rates) comes from
  a constant in `acquisition.h`, `sdr_dsp.h` or `gsm_dsp.h`; change one of those
  and the help is what goes stale.
- `src/band_plan.{c,h}` — the frequency-to-allocation table and its lookup. No
  DSP, no GUI, no receiver, and its own `make check-band-plan`, which walks the
  whole table for overlaps and unreachable entries because most of what goes
  wrong with a table is typing.
- `src/view_survey.c`, `src/survey_layout.h` — the band survey and its layout.
- `src/adsb_layout.h` — where the ADS-B decode view puts things, in the shape
  of `gsm_layout.h` and for the same reason: the analysis mode packs three
  charts over a log and a square scatter, and both modes' log rectangles are
  derived here so the view only picks one. Covered by `tests/layout_test.c`.
- `src/gsm_layout.h` — where the GSM decode view puts things: one struct of
  rectangles derived from the window size by a pure function, so the panels
  that share a row cannot drift apart and the whole layout is testable without
  a window. Covered by `tests/layout_test.c`.
- `src/sdrgui.h` and `sdrgui_plot.c` / `sdrgui_scope.c` / `sdrgui_decode.c` /
  `sdrgui_widgets.c` — reusable SDR visual components (plots,
  waterfall, scan chart, health circle, decoded-message log, cursor readouts)
  taking plain data/geometry, not `struct app`. Prefix `sdrgui_`. Split by what
  consumes them: `sdrgui_plot.c` holds the primitives the charts share,
  `sdrgui_scope.c` the four Scope views, `sdrgui_decode.c` the Decode tab's
  charts, `sdrgui_widgets.c` the two non-chart pieces. Every chart draws
  entirely inside the rectangle it is given. The presentation split is recorded
  in `docs/adr/0007-gui-presentation-layer.md`.
- `vendor/raygui.h` + `src/raygui_impl.c` — the vendored raygui immediate-mode
  widget toolkit (pinned; compiled once in isolation, `-Ivendor`), GUI build
  only. The DSP checks never link the GUI, so their `-lm`-only contract is
  unaffected.
- `tests/sdr_dsp_test.c` / `tests/gsm_dsp_test.c` / `tests/adsb_dsp_test.c` —
  the deterministic, hardware-free DSP checks (`make check-sdr-dsp` /
  `check-gsm-dsp` / `check-adsb-dsp`).
- `src/gsm_bcch.h` / `src/gsm_bcch.c` — the BCCH: four normal bursts to a
  System Information message. Interleaving, the (224,184) Fire code, the
  rate-1/2 convolutional code with a soft Viterbi, and what the message says --
  MCC, MNC, LAC, Cell Identity, neighbour ARFCNs. This is the Decoder context's
  side of GSM, where bits become a message, and it depends on nothing but
  the physical-layer constants in `gsm_dsp.h`. Fed by `gsm_normal_bursts()`,
  which does the coherent detection and equalisation a GMSK burst needs; on
  `testfiles/gsm_arfcn_69.bin` the pair report MCC 268 MNC 03, LAC 4010 and
  Cell Identity 5131. The GSM view shows the same thing on the line under the
  SCH readout, in a different colour: the SCH line is what was measured, the
  BCCH line is what the cell says, and the two should not be read as the same
  kind of claim.
- `tests/options_test.c`, `tests/calibration_gate_test.c`,
  `tests/survey_window_test.c`, `tests/layout_test.c`,
  `tests/band_plan_test.c` — the rest of the unit layer, one file per module.
  Each is a `main()` calling `test_*()` functions; there is no filter flag, so
  running one test alone means commenting out the others.
- `tests/check.h` — the whole framework: two counters, the comparisons every
  suite shares (`check_int`, `check_close`, `check_size`, `check_str`,
  `check_true`, and `check_msg(condition, fmt, ...)` when the values are worth
  printing in a shape the others cannot manage), and `check_report()`, which
  prints the suite's summary line and returns the exit code. Assertions state
  what must be *true*; the message says what went wrong.
- `tests/pipelines.sh` — the end-to-end layer: the built program driven through
  its command line over `testfiles/`, asserting on stdout. POSIX sh, no
  hardware, no window.
- Each capture has a `.json` sidecar recording the tuning it was taken at.
  Read it before using a capture: the GSM ones are tuned 400 kHz below their
  channel, and nothing in the samples says so. Sidecars written after the fact
  say `"provenance": "reconstructed"` and use `null` for fields that were never
  recorded — those are genuinely unknown, not zero.
- `testfiles/adsb_modes1.bin` — raw 8-bit I/Q capture at 2 MS/s, for
  hardware-free testing; read by `sdrprobe --file`.
- `testfiles/gsm_arfcn_69.bin` — 2 s raw I/Q capture of GSM 900 ARFCN 69
  (948.8 MHz, tuned to expected − 400 kHz); the `check-gsm-dsp` SCH test decodes
  its BSIC (59, NCC 7 / BCC 3).
- `testfiles/gsm_arfcn_73.bin` — 2 s raw I/Q capture of a second, weaker cell on
  ARFCN 73 (949.6 MHz, same − 400 kHz tuning), BSIC 56 (NCC 7 / BCC 0). Two
  independent cells matter here: the SCH field layout was derived from ARFCN 69,
  so ARFCN 73 is what checks it generalises rather than fitting one signal.
- `testfiles/gsm_arfcn_113.bin` — 2 s of ARFCN 113 (957.6 MHz), BSIC 38
  (NCC 4 / BCC 6), a different operator: MCC 268 MNC 06, LAC 8420, Cell 16134.
  It is here for the BCC. The other two are BCC 3 and BCC 0, and the BCC picks
  which of the eight training sequences a normal burst is found by, so this is
  the capture that says the burst demodulator works for more than the sequence
  it was written against — hardcoding sequence 3 passes ARFCN 69 and fails
  this. It is also the only GSM capture whose cell is still transmitting: 69
  and 73 are off the air, so those two cannot be re-recorded.

  Between them the three captures cover training sequences 0, 3 and 6, which
  is three of the eight. A live scan has since decoded a cell on BCC 2 as
  well, so the untested five are untested rather than unreachable: recording
  one of them would widen the check, and `--record-seconds` with whichever
  ARFCN the scan lands on is all it takes.
- `docs/ARCHITECTURE.md` — how this program is put together: the layers, what
  each may know, where state lives, and how changes are verified.
- `docs/dump1090-reference.md` — deep dive on dump1090 (threads, buffer
  overlap, CPR decoding). Reference only: that source is NOT in this repo.
- `docs/sdrprobe-implementation.md` — implementation and verification
  contract for the `sdrprobe` probe.

## Adding a view

Each screen has a view file, and they are built the same way; `view_lte.c`
with `lte_layout.h` is the most recent and the closest thing to a template.
The order that avoids rework:

1. **`src/<tech>_layout.h`** — header-only, one pure
   `<tech>_layout_for(float width, float height)` returning a struct of
   `Rectangle`s. It reads nothing from the window and calls no raylib
   function; that is what lets a check pin the geometry without opening one.
2. **`src/view_<tech>.c`** — draws from that struct and decides nothing.
   Declare its entry points in `src/view.h` beside the LTE set: `draw_`,
   `handle_<tech>_input`, `update_`, `view_<tech>_defaults`, and
   `enter_`/`leave_` if it borrows the receiver.
3. **`struct <tech>_view` in `src/app.h`**, reached as `app-><tech>.*`. State
   belonging to one view lives with it rather than loose in `struct app`.
4. **`enum decode_kind` in `src/app.h`** — extend it. An ad-hoc mode flag is
   the thing those enums replaced (ADR-0008).
5. **`tests/layout_test.c`** — include the header and pin the geometry.
6. **The Makefile** — add the header to `check-layout`'s prerequisites. This
   step has been missed once: the test binary is prebuilt, so a change to a
   header the rule does not name leaves the old checks in place, and they
   pass.
7. **Input** — add the dispatch at the end of the frame loop's precedence
   chain (CLAUDE.md sets out the order) rather than reading keys while
   drawing.

Then the rule that outranks the list: anything the view *decides* — a
threshold, a range, a pointer mapped to an index, a state machine advanced —
comes out into a named unit a check can reach (ADR-0012).
`src/sdrgui_geometry.h` is where that lands for anything positional, and
`sdrgui_bar_index_at()` is the worked example. It exists because a hit test
written inside a draw function selected the bar one or two to the right of the
pointer, the bars having been drawn inside a label gutter the hit test did not
know about, and there is no way to see that broken except by clicking.

## Seeing the window

`--screenshot <file>` writes the last frame of a windowed run to a PNG, which
the Read tool displays, so a change to a view can be looked at without a
person. It needs `--duration`, since the frame captured is the one before the
run ends. `.claude/skills/screenshot/SKILL.md` has the per-view recipes, how
long each needs to settle, and how to photograph a window that is already
open.

## Agent skills

### Validating DSP work

A round trip proves the code agrees with itself, not with the standard.
`.claude/skills/dsp-validation/SKILL.md` carries what counts as independent
corroboration of a real-signal answer, the two faults that hid behind green
checks, and which diagnostic tool answers which question.

### Calibrating against two references

`--lte-chain` walks the decode chain over a live cell and prints what every
stage produced per block, which is what `probe-lte-chain` does for a capture.
`--calibrate gsm|lte` runs one with no window and prints every residual, and
`--calibrate-band N` scans an LTE band and takes its strongest cell rather than
being told an EARFCN. The lock gate decides whether a correction may be
applied, so it has to be reachable without somebody clicking Start.

The calibration overlay takes 2G or 4G. GSM measures an FCCH tone; LTE takes
the offset the cell search already measures, and borrows 1.92 MS/s to do it.
Two circles at the top right, one per reference, because two independent
measurements of one crystal are worth more than either alone -- and the moment
they stop agreeing is the moment to distrust the correction.

### The RF environment

The survey is the default view and is reached by the Survey button at the left
of the Scope tab, not by a number. **Watch** repeats it, folding each sweep into
the site's history and saying what changed; `--survey-watch <n>` is the scripted
form. The history counts presence per hour of the day, which is what lets a
signal that follows the clock be told from one that merely comes and goes. A sweep's maxima are grouped into signals
first (`src/survey_carrier.h`), and
everything above the measurement works on those: a station has several maxima
and counting each is how one carrier becomes five things to remember. The
survey view remembers what each site has heard (`src/site_history.c`) and
marks the next sweep against it -- new signals, absent ones, and the history
under the cursor. Those marks are claims from a tenth of a second each, so
**Ask again** (`src/survey_confirm.h`, or `--survey-confirm` from a script)
revisits every one with six blocks on the frequency and records what it finds
rather than what the sweep guessed. Each site also keeps its own tuning
correction, since one measured elsewhere is not the one that applies here. It saves a finished sweep with a button, and the headless
`--survey` path prints one to be piped through `scripts/survey_tool.py ingest`;
both write the same file. `surveys/` keeps one JSON per band survey so sweeps
can be compared over time
-- gitignored, like `captures/`, because a sweep is one location with one
antenna. `docs/band-surveys.md` is the format,
and `.claude/skills/rf-environment/SKILL.md` reads them: what is on air, what
changed, and what is worth decoding next. It carries the two gates -- does the
signal fit the receiver, and is it actually there -- that 5G NR and DAB+ each
failed after looking like the obvious next thing.

### Issue tracker

Issues are tracked as local markdown under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five canonical role names as status strings. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.
