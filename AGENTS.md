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
- `make check-raylib-dsp` — builds and runs deterministic, hardware-free DSP
  checks; it does not require raylib. This is an alias for `make check-dsp`,
  which runs the per-technology checks `check-sdr-dsp` (generic core),
  `check-gsm-dsp` (GSM plugin), and `check-adsb-dsp` (Mode S / ADS-B plugin);
  each can be built and run on its own.
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
  Record button saves ~2 s of raw I/Q to `captures/` to build a GSM
  capture. The
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
- `src/sdrgui.h` / `src/sdrgui.c` — reusable SDR visual components (plots,
  waterfall, scan chart, health circle, decoded-message log, cursor readouts)
  taking plain data/geometry, not `struct app`. Prefix `sdrgui_`. `sdrprobe`
  composes these plus generic widgets; the presentation split is recorded in
  `docs/adr/0007-gui-presentation-layer.md`.
- `vendor/raygui.h` + `src/raygui_impl.c` — the vendored raygui immediate-mode
  widget toolkit (pinned; compiled once in isolation, `-Ivendor`), GUI build
  only. The DSP checks never link the GUI, so their `-lm`-only contract is
  unaffected.
- `tests/sdr_dsp_test.c` / `tests/gsm_dsp_test.c` / `tests/adsb_dsp_test.c` —
  the deterministic, hardware-free DSP checks (`make check-sdr-dsp` /
  `check-gsm-dsp` / `check-adsb-dsp`).
- `testfiles/adsb_modes1.bin` — raw 8-bit I/Q capture at 2 MS/s, for
  hardware-free testing; read by `sdrprobe --file`.
- `testfiles/gsm_arfcn_69.bin` — 2 s raw I/Q capture of GSM 900 ARFCN 69
  (948.8 MHz, tuned to expected − 400 kHz); the `check-gsm-dsp` SCH test decodes
  its BSIC (45, NCC 5 / BCC 5).
- `docs/ARCHITECTURE.md` — deep dive on dump1090 (threads, buffer overlap,
  CPR decoding). Reference only: the dump1090 source is NOT in this repo.
- `docs/sdrprobe-implementation.md` — implementation and verification
  contract for the `sdrprobe` probe.

## Agent skills

### Issue tracker

Issues are tracked as local markdown under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five canonical role names as status strings. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.
