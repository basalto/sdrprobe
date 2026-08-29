# AGENTS.md

Small C probes for RTL-SDR receivers, modeled after dump1090's
`modesInitRTLSDR()`. `src/rtl_init.c` is self-contained; `rtl_tui` splits its
terminal rendering into `src/tui.c`/`src/tui.h`, while `rtl_raylib` splits its
DSP into a generic core (`src/sdr_dsp.c`/`.h`) and per-technology plugins
(`src/gsm_dsp.c`/`.h`) (see "Files" below). No CI.

## Build & run

- `make` — builds `rtl_init` and `rtl_tui` via pkg-config; requires
  librtlsdr dev headers installed.
- `make rtl_raylib` — optionally builds the raylib visualizer; also requires
  raylib development headers. It is deliberately not part of default `make`.
- `make check-raylib-dsp` — builds and runs deterministic, hardware-free DSP
  checks; it does not require raylib. This is an alias for `make check-dsp`,
  which runs the per-technology checks `check-sdr-dsp` (generic core) and
  `check-gsm-dsp` (GSM plugin); each can be built and run on its own.
- `./rtl_init` needs a real RTL-SDR dongle; without one it prints
  "No supported RTLSDR devices found." and exits 1.
- `./rtl_tui` is a terminal UI plotting magnitude over time (ANSI escape
  codes on stderr, no curses). Needs a tty and (by default) a dongle;
  `./rtl_tui --file testfiles/modes1.bin` runs hardware-free, looping the
  capture. Quit with `q` or Ctrl-C.
- `./rtl_raylib [--frequency Hz|K|M|G] [--sample-rate S/s] [--gain max|auto|dB] [--ppm N]`
  opens magnitude, spectrum, I/Q scatter, and waterfall views.
  `--file capture.bin` enables paced, looping hardware-free playback; keys
  1/2/3/4 switch views. The Settings button (or `s`) changes center frequency
  and, for a live receiver, gain while running; it also toggles the default-on
  spectrum/waterfall DC-spike filter. The HUD reports noise, estimated SNR,
  clipping, and full-scale headroom for gain selection. Up/Down manually
  narrow/widen the active chart's scale. The Calibration button (or `c`), below
  Settings, opens GSM 900 cellular calibration for ARFCN 1-124, including
  expected/measured carrier markers and
  a stability-gated PPM suggestion. A top-right circle shows calibration health
  (grey uncalibrated, green FCCH-backed lock, amber checking, red drift); the
  optional Settings "Auto GSM drift check" periodically retunes to the
  calibrated ARFCN to re-verify and warns on drift (see
  `docs/adr/0006-gsm-drift-indicator.md`).
- Built binaries and the DSP check executables are gitignored (not tracked);
  `make` writes them to the repo root and `make clean` removes them.

## Signal conventions (match dump1090)

- 1090 MHz (ADS-B), 2 MS/s (1 sample = 0.5 µs), max manual gain.
- Samples are unsigned 8-bit interleaved I/Q; 127/127.5 = zero signal.
- Block size is `16*16384` = 256 KB, deliberately matching dump1090's block.

## Files

C sources and headers live in `src/`; hardware-free DSP test sources live in
`tests/`. Built binaries are written to the repo root.

- `src/tui.h` / `src/tui.c` — terminal UI layer used by `rtl_tui`: raw-mode
  termios, ANSI rendering of one frame of per-bin peak magnitudes, 'q' polling.
  No SDR types cross this seam; the ramp is scaled relative to each frame's
  noise floor (see the `tui_frame` comment in `src/tui.h`). The raylib probe
  uses a separate raw-I/Q seam because spectrum and I/Q scatter retain
  information that this magnitude-only interface discards.
- `src/sdr_dsp.h` / `src/sdr_dsp.c` — generic, technology-independent SDR DSP
  core: byte→float I/Q conversion, DC removal, magnitude peak binning, signal
  stats, Hann-windowed complex FFT / dBFS spectra, power-centroid carrier
  estimate, evenly-spaced channel-power reducer, and PPM correction. Prefix
  `sdr_dsp_`.
- `src/gsm_dsp.h` / `src/gsm_dsp.c` — GSM 900 technology plugin: ARFCN→frequency
  map and the FCCH reference-tone detector; reuses the generic core for
  everything else. Prefix `gsm_`. The per-technology DSP split (a generic core
  plus reference-tone plugins, calibration-grade scope) is recorded in
  `docs/adr/0001-technology-plugin-dsp-architecture.md`.
- `src/sdrgui.h` / `src/sdrgui.c` — reusable SDR visual components (plots,
  waterfall, scan chart, health circle, cursor readouts) taking plain
  data/geometry, not `struct app`. Prefix `sdrgui_`. `rtl_raylib` composes these
  plus generic widgets; the presentation split is recorded in
  `docs/adr/0007-gui-presentation-layer.md`.
- `vendor/raygui.h` + `src/raygui_impl.c` — the vendored raygui immediate-mode
  widget toolkit (pinned; compiled once in isolation, `-Ivendor`), GUI build
  only. The DSP checks never link the GUI, so their `-lm`-only contract is
  unaffected.
- `tests/sdr_dsp_test.c` / `tests/gsm_dsp_test.c` — the deterministic,
  hardware-free DSP checks (`make check-sdr-dsp` / `check-gsm-dsp`).
- `testfiles/modes1.bin` — raw 8-bit I/Q capture at 2 MS/s, for
  hardware-free testing; read by `rtl_tui --file`.
- `docs/ARCHITECTURE.md` — deep dive on dump1090 (threads, buffer overlap,
  CPR decoding). Reference only: the dump1090 source is NOT in this repo.
- `docs/rtl_raylib-implementation.md` — implementation and verification
  contract for the separate optional `rtl_raylib` probe. `src/rtl_init.c`
  remains the one-shot synchronous reader.

## Agent skills

### Issue tracker

Issues are tracked as local markdown under `.scratch/<feature>/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five canonical role names as status strings. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses a single-context layout. See `docs/agents/domain.md`.
