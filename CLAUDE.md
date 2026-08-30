# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` carries the exhaustive per-file and per-view tour (every CLI flag, key,
button, and file). This file covers the build/test loop, the big-picture
architecture, and the invariants that are easy to break. Read `AGENTS.md` when you
need the detail.

## Build & test

```sh
make                  # build ./sdrprobe (needs librtlsdr + raylib dev headers, pkg-config)
make check-dsp        # all three hardware-free DSP checks
make check-sdr-dsp    # one check in isolation — generic core
make check-gsm-dsp    # GSM plugin (+ the core it reuses)
make check-adsb-dsp   # Mode S / ADS-B plugin
make check-layout     # GSM view geometry (raylib headers only, no window)
make clean
```

There is no CI, no linter, and no test framework: each check is a `main()` that
calls `test_*()` functions and increments a file-static `failures` counter via
`check_*` helpers (`check_close`, `check_size`, `check_int`, `check_str`),
printing `... checks passed` and returning non-zero on failure. To run a
*single* test, temporarily comment out the other `test_*()` calls in that file's
`main()` — there is no filter flag.

White-box diagnostics (not tests — they print a walk through a decode chain and
compile the plugin's `.c` in to reach its statics):

```sh
make probe-gsm-chain                        # defaults to testfiles/gsm_arfcn_69.bin
make probe-gsm-chain FILE=captures/x.bin
make probe-adsb-chain FILE_ADSB=testfiles/adsb_modes1.bin
```

Running the app without hardware — always prefer this over asking for a dongle:

```sh
./sdrprobe --file testfiles/adsb_modes1.bin   # paced, looping playback
```

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
- `src/adsb_dsp.{c,h}` (`adsb_`) — Mode S: preamble detect, PPM bit demod, CRC-24,
  DF17/18 field parse, CPR position with an even/odd pairing cache.

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

- `src/sdrgui.{c,h}` (`sdrgui_`) — reusable visual components (plots, waterfall,
  scan chart, health circle, message log, cursor readouts). They take plain data
  and geometry and **never see `struct app`** (ADR-0007). They depend only on
  raylib.
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
`view_gsm.c`, `view_adsb.c`, `overlay_calibration.c`, `overlay_settings.c` —
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
  renderer drops blocks rather than lagging (ADR-0002). SIGINT/SIGTERM are
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
- Test captures are `testfiles/<tech>_<detail>.bin`. The two GSM captures must
  keep decoding their own BSIC in `check-gsm-dsp` — 59 (NCC 7 / BCC 3) for
  `gsm_arfcn_69.bin`, 56 (NCC 7 / BCC 0) for `gsm_arfcn_73.bin` — with frame
  numbers that increase and track the burst timeline. Those real-signal
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
- Deep-dive references: `docs/ARCHITECTURE.md` (dump1090 internals — that source
  is *not* in this repo), `docs/cellular-frequency-correction.md`,
  `docs/sch-frame-number-decode.md`, `docs/sdrprobe-implementation.md`.
