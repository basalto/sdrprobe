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
the site and the antenna recorded because levels only compare within one of
each. `survey_tool.py` reports one, diffs two, and refuses outright to diff
sweeps taken at different sites. The per-site history says what this place has
heard before, so a new sweep is annotated `new`, `steady`, `on/off` or `gone`
rather than being read cold.

**Decode.** Where a technology is understood, a decoder reads what the
transmitter is *saying* rather than merely measuring it:

| | reads |
| --- | --- |
| **FM broadcast** | stereo audio, and RDS down to the station's identification, name and programme type |
| **GSM 900** | the synchronisation burst's BSIC and frame number, then a System Information message: MCC, MNC, location area, cell identity |
| **Mode S / ADS-B** | aircraft address, altitude, and position from a CPR even/odd pair |
| **LTE** | the cell identity, its Master Information Block, reference power and quality (RSRP, RSRQ, RS-SINR), the channel's delay and drift, and how many antennas it transmits on |
| **TETRA** | the network's own identity from the broadcast layer: MCC, MNC, colour code, location area |

Every one of those ends in something a transmitter said about itself, which is
the bar for a decoder here rather than a demodulator.

**What it will not do.** Claim more than it measured. A level is dBFS and not
dBm, because nothing here knows the antenna's gain; a channel's delay spread is
placed among the 3GPP profiles only where the noise floor allows it and named
as unmeasurable where it does not; a cell identity is believed when its own
broadcast decodes and not when it merely repeats. The refusals are on screen,
because a reader who is not told cannot know the question was asked.

## What is on screen

- **Survey first.** The tab the program opens on: the sweep, its candidate
  list with each maximum's width, shape and what this site has heard of it
  before, a band picker over the 54 allocations the tuner reaches, and
  **Save survey** and **Watch** beside the site and antenna fields.
- **Four scope views** — magnitude over time, dBFS spectrum with average and
  peak hold, I/Q scatter, and a frequency/time waterfall. Cursor readouts
  everywhere; drag to zoom, `+`/`-`, `Left`/`Right` to pan, `0` to reset.
- **A decode view per technology** — FM, ADS-B, GSM, LTE and TETRA, each with
  two arrangements: the messages it has read, and the analysis behind them.
  `--analysis` opens on the second.
- **A decode funnel on every one of them.** Two empty panels look identical
  whether nothing is transmitting or every message is failing parity, and the
  funnel is the difference: blocks → bursts → parity → messages.
- **Signal-quality HUD** — noise floor, estimated SNR, clipping percentage and
  full-scale headroom, which is what gain selection needs.
- **Receiver calibration** — against a GSM FCCH tone or an LTE cell, with a
  median/MAD stability gate; the correction is kept per site, because it drifts
  and is measured against whatever reference a place offers.

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
./sdrprobe --headless --lte-chain --earfcn 3475 --lte-chain-seconds 30
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
make check           # all of the below, about a minute
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
src/  sdrprobe.c            application: acquisition, tabs, the frame loop
      acquisition.{c,h}     the receiver and file threads, and the block slot
      view_*.c              one file per screen: survey, scope, fm, gsm, adsb,
                            lte, tetra
      survey_*.{c,h}        the sweep, its candidates, the carriers they group
                            into, what resembles the receiver, the site history
      sdr_dsp.{c,h}         generic, technology-independent DSP core
      gsm_dsp.{c,h}  gsm_bcch.{c,h}     GSM: SCH, then System Information
      adsb_dsp.{c,h}                    Mode S: preamble, CRC-24, CPR
      fm_dsp.{c,h}   rds.{c,h}          FM: pilot, audio, and RDS groups
      lte_dsp.{c,h}  lte_mib.{c,h}      LTE: cell search, then the MIB
      tetra_dsp.{c,h} tetra_sync.{c,h}  TETRA: dibits, then the broadcast layer
      *_layout.h            where each screen puts things, as pure arithmetic
      sdrgui*.{c,h}         reusable chart components over vendored raygui
tests/      one check per area, each hardware-free -- see `make check`
scripts/    survey_tool.py, and the white-box probes behind `make probe-*`
vendor/     raygui.h        pinned immediate-mode widget toolkit
docs/       ARCHITECTURE.md, adr/, band-surveys.md, ...
testfiles/  test captures, one .json sidecar each
surveys/    saved sweeps and per-site history (gitignored)
build/      compiled artifacts (gitignored)
```

The DSP is split into a generic core and per-technology plugins, and a decoder
sits behind the same seam even where it reuses almost none of the core. The
domain is split in two and the split is load-bearing for naming: the **Probe**
context acquires samples and stops at signal statistics -- it must never claim
to have decoded a message -- and the **Decoder** context starts where bits
become a message. `CONTEXT-MAP.md` has both. The key decisions are recorded as ADRs in
[`docs/adr/`](docs/adr/), and the ubiquitous language in
[`CONTEXT.md`](CONTEXT.md).

## License

MIT — see [`LICENSE`](LICENSE).
