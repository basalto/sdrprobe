# sdrprobe

A [raylib](https://www.raylib.com/) signal probe for RTL‑SDR receivers: it
acquires raw I/Q, visualizes it four ways, and calibrates the receiver's
frequency error against a GSM 900 cellular reference. Acquisition is modeled
after dump1090's `modesInitRTLSDR()`.

> Scope today is **calibration‑grade detection** — identify a reference carrier
> and measure its frequency — not message decoding. The architecture (a generic
> DSP core plus per‑technology plugins) is built to grow toward decoding.

## Features

- **Five live views** — magnitude over time, dBFS spectrum (average + peak
  hold), I/Q scatter, a frequency/time waterfall, and a band survey. Cursor
  readouts on every plot; Up/Down rescales the active chart.
- **Band survey** — sweep any range up to the tuner's full span, chart the
  power across it, and mark the peaks standing above their local noise floor.
  Pick one and it retunes and measures it: occupied bandwidth, prominence,
  duty (continuous, intermittent, bursty) and frequency stability, plus the
  allocation the frequency falls in — a band-plan lookup, never a claim about
  what the signal *is*. Portuguese/European allocations are shaded behind the
  trace; drag a rectangle to zoom, `+`/`-` zoom, `Left`/`Right` pan, `0`
  resets, and the candidate list follows the window. A dwell field decides how
  long each step listens, which is what catches transmitters that are not
  always on, Sweep surveys whatever the chart is showing — the fields when zoomed
  out, the window when zoomed in — "Scan this frequency" drills into a selected
  candidate, "Open waterfall" watches it over time, and "Reset zoom" backs out
  again.
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

## Versioning

`sdrprobe --version`, and the same string in the window's bottom-right corner.

It follows [Semantic Versioning 2.0.0](https://semver.org). For an application
rather than a library, the three numbers are read against the interfaces other
people's work can break against, not against C symbols nobody links to:

- **the command line** — flag names, their values, what they refuse;
- **the headless reports** other programs parse — `--decode`, `--survey`,
  `--lte-scan`, `--lte-chain`, `--calibrate`, and the `candidate` and `survey`
  record lines behind `scripts/survey_tool.py`;
- **the files kept between runs** — `~/.config/sdrprobe/config`, the survey
  JSON under `surveys/`, `surveys/history-<site>.txt`, and capture sidecars.

MAJOR when one of those breaks, MINOR when one gains something backwards
compatible, PATCH when behaviour is corrected without either. The screens are
not on that list: a new view or a moved panel is MINOR at most, because
nothing can depend on them programmatically. And a decode that starts reading
a field it previously got wrong is a PATCH — the wrong answer was never the
contract.

Still `0.x` deliberately, which under SemVer says the public surface may still
move without a MAJOR bump. It has, recently. `docs/adr/0016` sets out what
1.0.0 would be promising.

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
           [--device index]
           [--view magnitude|spectrum|scatter|waterfall|survey|gsm|adsb]
           [--survey-range low:high]
           [--record-seconds n] [--technology gsm|adsb|raw] [--arfcn 1-124]
           [--gsm-features list] [--dc-filter on|off] [--duration n] [--once]
           [--headless] [--decode] [--list-devices]
```

Scripted use, no window and no clicking:

```sh
./sdrprobe --list-devices                       # what is attached, and is it free
./sdrprobe --headless --record-seconds 3 \
           --technology adsb                    # capture 3 s + sidecar, print the path
./sdrprobe --view adsb --duration 20            # open on a screen, quit by itself
./sdrprobe --survey-range 88M:108M              # sweep a band and show what is on it
./sdrprobe --headless --arfcn 73 --record-seconds 2   # a GSM channel, sidecar and all

# Decode a capture with no window and no clicking:
./sdrprobe --file testfiles/adsb_cpr_pair.bin \
           --headless --technology adsb --decode --once
./sdrprobe --file testfiles/gsm_arfcn_73.bin \
           --headless --arfcn 73 --decode --once
#   SCH  BSIC 56 (NCC 7, BCC 0)  frame 2090358 (T1/T2/T3 1576/10/21)  match 0.87

# What each SCH refinement is worth, measured rather than assumed:
for f in none filter filter,finecfo,trellis; do
  ./sdrprobe --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 \
             --decode --once --gsm-features $f | grep -c SCH
done   # 6, 13, 29
```

A recording lands in `captures/<technology>_<stamp>.bin` with a `.json` sidecar
recording the tuning it was taken at, so a capture never has to be explained in
prose afterwards.

Keys and controls:

| Input | Action |
| --- | --- |
| `1` `2` `3` `4` `5` | magnitude / spectrum / scatter / waterfall / band survey |
| `Up` / `Down` | narrow / widen the active chart's scale |
| `s` or Settings button | change frequency, gain, PPM, DC filter |
| `c` or Calibration button | open GSM 900 calibration |
| `h` | help: what each chart plots and how to read it |
| Record 2s button | save raw I/Q + sidecar to `captures/` (GSM and ADS-B views) |
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

Everything is checkable without a window, a receiver, or a person:

```sh
make check           # all of the below, about 18 seconds
```

```sh
make check-dsp         # generic core, GSM, Mode S, band plan
make check-options     # the command line: every flag, value, and rejection
make check-survey      # the band survey's zoom, pan and sweep arithmetic
make check-calibration # when a frequency correction may be trusted
make check-layout      # view geometry at several window sizes
make check-pipelines   # the built program over testfiles/, asserting on stdout
```

The unit checks link only `-lm` (no raylib, no librtlsdr) and never touch the
GUI. `check-pipelines` runs the real binary against the recorded captures in
`testfiles/`: both GSM captures must decode their own BSIC, the ADS-B capture
must resolve a position from an even/odd pair, and it must do so identically
twice.

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
