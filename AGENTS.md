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
  `check-gsm-dsp` (GSM plugin), and `check-adsb-dsp` (Mode S / ADS-B plugin);
  each can be built and run on its own.
- `make check-layout` — pins the GSM decode view's rectangles and the window chrome at
  several window sizes. Needs raylib's headers for the `Rectangle` type but not the
  library, and opens no window. A failure means geometry moved; if that was
  intended, re-bless the numbers in `tests/layout_test.c` in the same
  commit, so the diff shows what shifted.
- `./sdrprobe [--frequency Hz|K|M|G] [--sample-rate S/s] [--gain max|auto|dB] [--ppm N]`
  opens magnitude, spectrum, I/Q scatter, and waterfall views. It needs a real
  RTL-SDR dongle by default; `--file capture.bin` (e.g.
  `testfiles/adsb_modes1.bin`) enables paced, looping hardware-free playback. A
  top-of-window tab bar switches between Scope and Decode. In the Scope tab,
  keys 1/2/3/4 switch views; in the Decode tab, keys 1/2 switch between GSM band
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
  Record button saves ~2 s of raw I/Q to `captures/` to build a GSM capture,
  writing a `.json` sidecar beside it with the tuning the samples were taken
  at (centre frequency, sample rate, gain, PPM, tuner, the GSM ARFCN and its
  carrier offset) plus a short-block count that is non-zero only when the
  capture is not contiguous. The
  Calibration button (or `c`), below Settings, opens button-driven GSM 900
  cellular calibration for ARFCN 1-124 as a full-screen overlay over any tab,
  including expected/measured carrier markers and a stability-gated PPM
  suggestion. The ADS-B view decodes 1090 MHz Mode S extended squitters
  (DF17/18) and shows a newest-first log of decoded messages (time, ICAO, decoded
  fields, and the raw hex frame); it offers a retune-to-1090 affordance on a
  live receiver when tuned elsewhere. A top-right
  circle shows calibration health (grey uncalibrated, green FCCH-backed lock,
  amber checking, red drift); the optional Settings "Auto GSM drift check"
  periodically retunes to the calibrated ARFCN to re-verify and warns on drift
  (see `docs/adr/0006-gsm-drift-indicator.md`). Quit with `q`, Esc, or Ctrl-C.
- Built binaries and the DSP check executables are gitignored (not tracked);
  `make` writes them to the repo root and `make clean` removes them.

## Signal conventions (match dump1090)

- 1090 MHz (ADS-B), 2 MS/s (1 sample = 0.5 µs), max manual gain.
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
