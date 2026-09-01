# AGENTS.md

`sdrprobe` is a raylib SDR visualizer and GSM 900 frequency-calibration probe
for RTL-SDR receivers, modeled after dump1090's `modesInitRTLSDR()` acquisition.
Its DSP is split into a generic core (`src/sdr_dsp.c`/`.h`) and per-technology
plugins (`src/gsm_dsp.c`/`.h` for GSM calibration, `src/adsb_dsp.c`/`.h` for
Mode S / ADS-B message decoding), and its UI into an SDR component layer
(`src/sdrgui.c`/`.h`) over vendored raygui widgets (see "Files" below). The
window is organised into two top-level tabs — Scope (the four signal views,
keys 1-4) and Decode (message decoders selected by number keys: 1 GSM band
analysis, 2 ADS-B) — recorded in
`docs/adr/0008-top-level-tab-navigation.md`. Calibration remains a button-driven,
global full-screen overlay orthogonal to the tabs. Message decoding lives in a
second bounded context (see `CONTEXT-MAP.md`). No CI.

## Build & run

- `make` — builds `sdrprobe` via pkg-config; requires librtlsdr and raylib
  development headers installed.
- `make check-dsp` — builds and runs deterministic, hardware-free DSP
  checks; it does not require raylib. It runs the per-technology checks
  `check-sdr-dsp` (generic core),
  `check-gsm-dsp` (GSM plugin), `check-adsb-dsp` (Mode S / ADS-B plugin) and
  `check-band-plan` (the frequency allocation table); each can be built and run
  on its own.
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
  `docs/adr/0011-band-plan-is-a-lookup.md`, which is the boundary this view
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
  span of zero.
  `--survey-range low:high` opens the view and sweeps that
  range without waiting to be asked, and `--survey-dwell` sets the dwell; in the Decode tab, keys 1/2 switch between GSM band
  analysis and ADS-B. The Settings button (or `s`) changes center frequency and, for a live receiver, gain while running; it also toggles the
  default-on spectrum/waterfall DC-spike filter. The HUD reports noise,
  estimated SNR, clipping, and full-scale headroom for gain selection. Up/Down
  manually narrow/widen the active chart's scale. The Decode tab's GSM view
  surveys GSM 900: a channel-power scan (with BCCH/FCCH highlighting) and an
  ARFCN-axis waterfall; it opens on the calibrated channel (or the last selected
  one, otherwise it scans and picks the strongest BCCH), and clicking a channel
  tunes the waterfall above to inspect it. On the inspected channel it decodes
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
- `src/view_gsm.c`, `src/view_adsb.c` — the Decode tab's two screens.
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
- `docs/ARCHITECTURE.md` — how this program is put together: the layers, what
  each may know, where state lives, and how changes are verified.
- `docs/dump1090-reference.md` — deep dive on dump1090 (threads, buffer
  overlap, CPR decoding). Reference only: that source is NOT in this repo.
- `docs/sdrprobe-implementation.md` — implementation and verification
  contract for the `sdrprobe` probe.

## Agent skills

### Issue tracker

Issues are tracked as local markdown under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five canonical role names as status strings. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.
