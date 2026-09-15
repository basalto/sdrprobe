# sdrprobe

An RF awareness probe for RTL-SDR receivers. It surveys the spectrum to find
out **what is transmitting, where, and when** -- then identifies what it can
and hands the rest to a decoder that reads the signal's own account of itself.

The four things it does, in the order it does them:

**Survey.** Sweep any range the tuner reaches, fold each step into a
peak-hold, and report the maxima that stand above their local noise floor --
with the frequency, the level, how wide the sweep could see them, and the
allocation the frequency falls in. That last one is a **band-plan lookup and
never a claim about what the signal is**: band 28 here is labelled an LTE
downlink and carries 5G NR (ADR-0015).

**Watch.** A sweep step is a tenth of a second, so its marks are claims rather
than findings. `--survey-confirm` revisits each with six looks and returns
`confirmed`, `intermittent` or `refuted`; **Watch** keeps sweeping and folds
every pass into the site's history, so a signal that is only there in the
evenings reads as `by hour` rather than `on/off`. Bursty and intermittent
traffic is the thing a single sweep cannot see and this is how it is caught.

**Remember.** Sweeps accumulate under `surveys/` as JSON, one per pass, with
the receiver, site and antenna recorded because levels and baselines only
compare within one receiving setup. `survey_tool.py` reports one, diffs two,
and refuses outright to compare incompatible setups. The setup's history says
what it has heard before, so a new sweep is annotated `new`, `steady`,
`on/off` or `gone` rather than being read cold.

**Decode.** Where a technology is understood, a decoder reads what the
transmitter is *saying* rather than merely measuring it:

| | reads |
| --- | --- |
| **FM broadcast** | stereo audio, and RDS down to the station's identification, name and programme type |
| **GSM 900** | the synchronisation burst's BSIC and frame number, then a System Information message: MCC, MNC, location area, cell identity |
| **Mode S / ADS-B** | aircraft address, altitude, and position from a CPR even/odd pair |
| **LTE** | the cell identity, its Master Information Block, reference power and quality (RSRP, RSRQ, RS-SINR), the channel's delay and drift, and how many antennas it transmits on |
| **TETRA** | the network's own identity from the broadcast layer: MCC, MNC, colour code, location area |
| **SRD 433-435 MHz** | OOK/Manchester full and repeat frames; 2-FSK transmissions are detected and reported, not yet decoded |

Every one of those ends in something a transmitter said about itself, which is
the bar for a decoder here rather than a demodulator.

## What is on screen

Three peer tabs organize the window: **Survey**, **Scope**, and **Decode**.
Survey is the default; Scope's number keys select four ways to inspect the
current tuning, while Decode's select six technology views. Settings,
Calibration, Help, and the startup form are overlays over that navigation.

- **Survey.** The tab the program opens on: the sweep, its candidate
  list with each maximum's width, shape and what this site has heard of it
  before, a band picker over the 54 allocations the tuner reaches, and
  **Save survey** and **Watch** beside the site and antenna fields.
- **Four scope views** — magnitude over time, dBFS spectrum with average and
  peak hold, I/Q scatter, and a frequency/time waterfall. Cursor readouts
  everywhere; drag to zoom, `+`/`-`, `Left`/`Right` to pan, `0` to reset.
- **A decode view per technology** — FM, ADS-B, GSM, LTE, TETRA and SRD.
  Each has data and analysis arrangements; `--analysis` opens on the second.
- **A decode funnel on every one of them.** Two empty panels look identical
  whether nothing is transmitting or every message is failing parity, and the
  funnel is the difference: blocks → bursts → parity → messages.
- **Signal-quality HUD** — noise floor, estimated SNR, clipping percentage and
  full-scale headroom, which is what gain selection needs.
- **Receiver calibration** — against a GSM FCCH tone or an LTE cell, with a
  median/MAD stability gate. A correction belongs to one receiver at one site:
  it compensates that receiver's crystal and records where the reference was
  measured.
- **Retrospective signal inspection** — acquisition keeps recent raw I/Q in a
  ring. Right-click a waterfall to save the past two seconds or run a signal
  report without interrupting reception.

`--startup` asks for the site, antenna, and receiver identity while it looks
for a calibration reference: GSM first, then a reachable LTE band when no GSM
broadcast carrier is found. Those installation facts persist in
`~/.config/sdrprobe/config`, so a receiver that has not moved keeps the
correction it was given and the health indicator says what that is.

It is opt-in because it costs a cold launch 12.8 s of GSM scanning where GSM
900 is on air, and minutes of LTE band scan where it is not (ADR-0024, amended
2026-09-15). A launcher that cannot reach the command line can use
`SDRPROBE_STARTUP`; `--no-startup` still refuses, and beats a request from
either route.

## Without a window

Every decision the program makes is reachable from the command line, because
one that needs a person to click cannot be checked (ADR-0012):

```sh
# Sweep, confirm what it found, and file the result under surveys/
./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
    --survey-confirm | ./scripts/survey_tool.py ingest --note "telescopic, indoors"

# Keep sweeping, folding each pass into the site's history
./sdrprobe --headless --survey --survey-watch 20

# Read a capture, or a live cell, with nothing to click
./sdrprobe --file testfiles/gsm_arfcn_69.bin --headless --arfcn 69 --decode --once
# Walk every LTE identity on one carrier, not only the strongest
./sdrprobe --headless --lte-chain --earfcn 3475 --lte-chain-seconds 30
# Find cells across a band, or find and measure a calibration reference
./sdrprobe --headless --lte-scan 20
./sdrprobe --headless --calibrate auto --site home
```

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
  JSON under `surveys/`,
  `surveys/history-<receiver>-<site>-<antenna>.txt`, and capture sidecars.

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
./sdrprobe           # live receiver; Survey opens after installation startup
./sdrprobe --file testfiles/adsb_modes1.bin   # hardware-free paced playback
```

```
./sdrprobe [--frequency Hz|K|M|G] [--sample-rate samples_per_second]
           [--gain max|auto|dB] [--ppm signed_integer] [--file capture.bin]
           [--device index]
           [--view magnitude|spectrum|scatter|waterfall|survey|fm|adsb|gsm|lte|tetra|srd]
           [--record-seconds n] [--technology fm|adsb|gsm|lte|tetra|srd|raw]
           [--antenna name] [--site name] [--startup]
           [--arfcn 1-124] [--earfcn n] [--lte-scan band]
           [--survey-range low:high] [--survey-dwell seconds]
           [--duration n] [--once] [--headless] [--decode]
```

`./sdrprobe --help` is the built-in option reference. Scripted calibration,
LTE chain analysis, screenshots, debug logging, analysis mode, Scope FFT size,
and survey selection controls are catalogued in [`AGENTS.md`](AGENTS.md).

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
| Survey / Scope / Decode tabs | switch the top-level activity |
| Scope: `1` `2` `3` `4` | magnitude / spectrum / scatter / waterfall |
| Decode: `1` ... `6` | FM / ADS-B / GSM / LTE / TETRA / SRD |
| `Up` / `Down` | narrow / widen the active chart's scale |
| `s` or Settings button | change frequency, gain, PPM, DC filter |
| `c` or Calibration button | open GSM or LTE calibration |
| `h` | help: what each chart plots and how to read it |
| Record button | save raw I/Q + sidecar to `captures/` from a decode view |
| Right-click a waterfall | save or inspect recent raw I/Q from that point |
| `q`, `Esc`, `Ctrl‑C` | quit |

## Calibrating the receiver

The startup form (`--startup`) finds a reference automatically: it verifies a
GSM broadcast carrier and measures its FCCH, falling back to an LTE band scan
when GSM is unavailable. It asks for two independent references before applying an
automatically selected correction. A named `--arfcn` or `--earfcn` remains an
explicit instruction and is not second-guessed.

For a manual run, press **Calibration**, choose 2G or 4G, select a channel or
band, and wait for the stability gate. **Apply PPM** records the correction for
this receiver and site. The top-right health indicators show which references
remain valid; the optional GSM drift check periodically re-verifies an
FCCH-backed correction.

See [`docs/cellular-frequency-correction.md`](docs/cellular-frequency-correction.md)
for the full procedure and the DSP details.

## Testing

Everything is checkable without a window, a receiver, or a person:

```sh
make check           # complete gate, no window or receiver
make check-touched   # suites selected from the files changed
```

```sh
make check-dsp         # generic core and technology DSP modules
make check-options     # the command line: every flag, value, and rejection
make check-survey-session # sweep, confirmation, watch and measurement
make check-calibration # when a frequency correction may be trusted
make check-receiver-runtime # receiver transitions and rollback
make check-signal-frame # one converted and measured sample block
make check-layout      # view geometry at several window sizes
make check-pipelines   # the built program over testfiles/, asserting on stdout
```

The decision checks need no window, receiver, or person. DSP and domain suites
keep raylib and librtlsdr out of their dependency boundary. `check-pipelines`
runs the real binary against the external capture corpus: GSM, ADS-B, LTE, TETRA, FM,
survey, recording, and both supported sample containers must keep their
end-to-end invariants.

## Project layout

```
src/  sdrprobe.c            process lifecycle, tabs, frame and headless loops
  device_backend.h      receiver/capture operations behind one seam
  device_profile.h      format, full scale, reach, gain and reference clock
  acquisition.{c,h}     worker, block slot, recording and retrospective I/Q
  signal_frame.{c,h}    one converted and measured sample block
  receiver_runtime.*    checked retune transaction and rollback
  receiver_lease.h      nested temporary ownership of receiver settings
      view_*.c              one file per screen: survey, scope, fm, gsm, adsb,
            lte, tetra, srd
  survey_session.*      sweep, confirmation, watch and measurement machine
  survey_record.*       immutable finished survey before text or JSON
  startup_session.*     GSM-first, LTE-fallback installation calibration
      sdr_dsp.{c,h}         generic, technology-independent DSP core
      gsm_dsp.{c,h}  gsm_bcch.{c,h}     GSM: SCH, then System Information
      adsb_dsp.{c,h}                    Mode S: preamble, CRC-24, CPR
      fm_dsp.{c,h}   rds.{c,h}          FM: pilot, audio, and RDS groups
      lte_dsp.{c,h}  lte_mib.{c,h}      LTE: cell search, then the MIB
      tetra_dsp.{c,h} tetra_sync.{c,h}  TETRA: dibits, then the broadcast layer
  srd_dsp.{c,h}  srd_frame.{c,h}    SRD: OOK/2-FSK shape, Manchester frames
      *_layout.h            where each screen puts things, as pure arithmetic
      sdrgui*.{c,h}         reusable chart components over vendored raygui
tests/      one check per area, each hardware-free -- see `make check`
scripts/    survey_tool.py, and the white-box probes behind `make probe-*`
vendor/     raygui.h        pinned immediate-mode widget toolkit
docs/       ARCHITECTURE.md, adr/, band-surveys.md, ...
testfiles/  test captures, one .json sidecar each
surveys/    saved sweeps and receiving-setup history (gitignored)
build/      compiled artifacts (gitignored)
```

The DSP is split into a generic core and per-technology modules, and a decoder
sits behind the same seam even where it reuses almost none of the core. The
domain is split in two and the split is load-bearing for naming: the **Probe**
context acquires, surveys and measures signals, stopping before modulation is
interpreted as transmitted information; the **Decoder** context owns that
interpretation. `CONTEXT-MAP.md` has both. The key decisions are recorded as ADRs in
[`docs/adr/`](docs/adr/), and the ubiquitous language in
[`CONTEXT.md`](CONTEXT.md).

## What it will not do

Claim more than it measured.

A level is **dBFS and not dBm**, because nothing here knows the antenna's gain
or the cable's loss -- so it compares one cell with another on this receiver at
this gain, and means nothing across installations. RSRQ and RS-SINR are ratios
through the same chain and do carry across, which is why they are worth reading
beside a level that does not.

A channel's **delay spread is placed among the 3GPP profiles only where the
noise floor allows it**. The estimator measures the scatter of the phase steps
between reference signals, so its floor is the signal-to-noise -- 559 ns at
10 dB, 70 at 28 -- and its ceiling is where those steps wrap, at 1768 ns. Above
that the number is not a delay, and it says so rather than printing a profile
name a reader would have believed.

A **cell identity is believed when its own broadcast decodes**, not when it
repeats. A search that mistakes a sidelobe for a cell makes the same mistake
every block, so a false identity repeats exactly as faithfully as a true one --
on one carrier here, twenty-nine sightings and not one message. A Master
Information Block is scrambled with the identity and checked by a CRC, and that
cannot be repeated into existence.

An **allocation is a lookup, not an identification** (ADR-0015). Band 28 here
is labelled an LTE downlink and carries 5G NR.

The refusals are on screen and in the headless reports rather than left out,
because a reader who is not told cannot know the question was asked.

## License

MIT — see [`LICENSE`](LICENSE).
