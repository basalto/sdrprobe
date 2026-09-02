# TODO

## Proposed Features

- [ ] Add a lightweight ADS-B burst detector that marks probable pulse bursts
  without decoding messages.
- [ ] Add gain comparison mode to test supported gains and rank them by
  estimated SNR, clipping, and headroom.
- [x] Add frequency correction with `--ppm`, a Settings field, and applied
  correction in the HUD.
- [x] In frequency correction window, label the x axis with ARFCN channel
  numbers instead of frequency, and allow changing the target calibration
  ARFCN while running (press Start/"Retune") without exiting and restarting.
- [ ] Add a stable internal sample-clock estimator that suggests a PPM
  correction after at least 60 seconds when `|PPM| >= 10`.
- [x] Add raw I/Q recording with center frequency, sample rate, gain, PPM,
  tuner identity, and timestamps as metadata (a JSON sidecar beside each
  capture; also records the GSM carrier offset and a short-block count).
- [ ] Add pause, resume, and clear controls for waterfall history.
- [ ] Add configurable waterfall history duration.
- [ ] Add persistent-spur detection and distinguish signals that follow the
  tuning center from signals that remain at an absolute frequency.
- [ ] Add device selection by index or serial when multiple RTL-SDR receivers
  are connected.
- [ ] Add optional Mode S/ADS-B preamble and valid-message counters, while
  keeping full aircraft tracking and networking out of this probe.

- [ ] Read an LTE Master Information Block off the air. The chain is written
  and checked (`make check-lte-mib`) and recovers a synthesised broadcast
  channel exactly, but no live capture decodes: the cell-specific reference
  signals do not lock, so there is no channel estimate behind the soft bits.
  See `.scratch/lte-cell-search/issues/05-the-broadcast-channel-on-air.md` —
  the next step is a stronger capture, which separates "too weak" from "a
  convention is wrong" in one measurement.
- [ ] Add a sample rate to the Settings panel, so the LTE view can be reached
  without restarting. LTE needs 1.92 MS/s (ADR-0014) and nothing at runtime
  can change the rate today.
- [ ] Extended-cyclic-prefix LTE. The cell search reports it; the broadcast
  channel declines it, because that prefix puts a third reference symbol
  inside the channel and shortens it to 432 bits. No commercial FDD cell uses
  it, and there is no capture to check it against.

## Implemented

- [x] Magnitude, spectrum, I/Q scatter, and waterfall views.
- [x] Live receiver and paced looping capture playback.
- [x] Runtime center-frequency and gain settings.
- [x] Frequency input using plain Hz or `K`/`M`/`G` suffixes.
- [x] Optional per-block DC removal for spectrum and waterfall.
- [x] Manual chart scales controlled by Up/Down.
- [x] Retained waterfall dBFS history that can be recolored after scale changes.
- [x] Cursor crosshairs with axis values and center-frequency offsets.
- [x] Noise p10, signal p99.5, estimated SNR, clipping, and headroom metrics.
- [x] Deterministic, hardware-free DSP checks.
- [x] LTE cell search: EARFCN map, PSS, SSS, physical cell identity, frame
  boundary, cyclic-prefix length, and a frequency offset good to tens of hertz.
  Verified against three live band 20 carriers.
- [x] Add GSM 900 ARFCN calibration with expected/measured waterfall markers,
  confidence reporting, stability gating, and PPM application.
