# sdrprobe

A [raylib](https://www.raylib.com/) signal probe for RTL‑SDR receivers: it
acquires raw I/Q, visualizes it four ways, and calibrates the receiver's
frequency error against a GSM 900 cellular reference. Acquisition is modeled
after dump1090's `modesInitRTLSDR()`.

> Scope today is **calibration‑grade detection** — identify a reference carrier
> and measure its frequency — not message decoding. The architecture (a generic
> DSP core plus per‑technology plugins) is built to grow toward decoding.

## Features

- **Four live views** — magnitude over time, dBFS spectrum (average + peak
  hold), I/Q scatter, and a frequency/time waterfall. Cursor readouts on every
  plot; Up/Down rescales the active chart.
- **Live or file** — a real RTL‑SDR dongle, or paced, looping hardware‑free
  playback of a raw capture (`--file`).
- **Runtime settings** — center frequency (Hz or `K`/`M`/`G`), gain (live),
  PPM correction, and a default‑on spectrum/waterfall DC‑spike filter.
- **Signal‑quality HUD** — noise floor, estimated SNR, clipping %, and
  full‑scale headroom to guide gain selection.
- **GSM 900 frequency calibration** — measures a known ARFCN's carrier with a
  power centroid refined by the **FCCH pure tone**, applies a robust
  median/MAD + standard‑error stability gate, and suggests a PPM correction.
- **Channel power scan** — sweeps the GSM 900 downlink band and charts
  per‑ARFCN power, flagging **BCCH** channels (those carrying an FCCH tone) so
  you can pick a good calibration reference.
- **ADS‑B frame analysis** — an analysis mode beside the message log charting
  one Mode S frame's decode: preamble score landscape, per‑bit decision
  confidence, magnitude envelope, and a bit‑decision scatter, plus a decode
  funnel (preambles → squitter‑shaped → CRC failed → decoded) that says whether
  an empty log means a silent band or frames that are failing.
- **Calibration‑health indicator** — a top‑right circle (grey/green/red) plus an
  opt‑in periodic drift re‑check that retunes to the calibrated ARFCN and warns
  on drift.

## Requirements

- `librtlsdr` and `raylib` development headers
- `pkg-config`, a C compiler, and `make`
- An RTL‑SDR dongle for live use (not needed for `--file` playback or the tests)

On Arch‑based systems: `pacman -S rtl-sdr raylib pkgconf`.

## Build & run

```sh
make                 # builds ./sdrprobe
./sdrprobe           # live receiver, defaults to 1090 MHz / 2 MS/s / ~30 dB gain
./sdrprobe --file testfiles/adsb_modes1.bin   # hardware-free paced playback
```

```
./sdrprobe [--frequency Hz|K|M|G] [--sample-rate samples_per_second]
           [--gain max|auto|dB] [--ppm signed_integer] [--file capture.bin]
```

Keys and controls:

| Input | Action |
| --- | --- |
| `1` `2` `3` `4` | magnitude / spectrum / scatter / waterfall view |
| `Up` / `Down` | narrow / widen the active chart's scale |
| `s` or Settings button | change frequency, gain, PPM, DC filter |
| `c` or Calibration button | open GSM 900 calibration |
| `h` | help: what each chart plots and how to read it |
| `q`, `Esc`, `Ctrl‑C` | quit |

## Calibrating the receiver

1. Press **Calibration**, then **Scan** to sweep the band. Green bars are
   BCCH channels (an FCCH tone was detected).
2. Click a green channel — calibration retunes to it and starts measuring.
3. Wait for **Stable lock (FCCH tone)**; the correction uncertainty falls below
   1 PPM.
4. Press **Apply PPM**. The top‑right circle turns green. Optionally enable
   *Auto GSM drift check* in Settings to re‑verify periodically.

See [`docs/cellular-frequency-correction.md`](docs/cellular-frequency-correction.md)
for the full procedure and the DSP details.

## Testing

The DSP is hardware‑free and deterministic:

```sh
make check-dsp       # runs the generic-core and GSM-plugin checks
make check-sdr-dsp   # generic SDR primitives only
make check-gsm-dsp   # GSM plugin (+ the generic core it reuses)
```

The checks link only `-lm` (no raylib, no librtlsdr) and never touch the GUI.

## Project layout

```
src/       sdrprobe.c        application: state, acquisition, screen composition
           sdr_dsp.{c,h}     generic, technology-independent SDR DSP core
           gsm_dsp.{c,h}     GSM 900 plugin: ARFCN map + FCCH tone detector
           sdrgui.{c,h}      reusable SDR visual components (plots, waterfall, ...)
           raygui_impl.c     one TU that expands the vendored raygui
tests/     sdr_dsp_test.c    hardware-free DSP checks
           gsm_dsp_test.c
vendor/    raygui.h          pinned immediate-mode widget toolkit
docs/      ARCHITECTURE.md, adr/, cellular-frequency-correction.md, ...
testfiles/ adsb_modes1.bin, gsm_arfcn_69.bin   test captures (&lt;tech&gt;_&lt;detail&gt;.bin)
build/     compiled artifacts (gitignored)
```

The DSP is split into a generic core and per‑technology plugins; the UI is split
into reusable `sdrgui` components over vendored raygui widgets, with application
logic and composition in `sdrprobe.c`. The key decisions are recorded as ADRs in
[`docs/adr/`](docs/adr/), and the ubiquitous language in
[`CONTEXT.md`](CONTEXT.md).

## License

MIT — see [`LICENSE`](LICENSE).
