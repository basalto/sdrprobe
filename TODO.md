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

- [ ] The LTE band scan misses a cell that only decodes in about half its
  blocks: it looks at two blocks per channel, so a quarter of the time it sees
  neither. A third look would halve that and cost thirty seconds a band. The
  live band 20 carrier at 816 MHz is the one that comes and goes.
- [ ] Weak entries in the LTE scan list are not always real. Confirming an
  identity twice removed the three obvious false positives, and the list is
  ordered by correlation so the doubtful ones sink, but a repeatable artefact
  would still survive. Revisiting each found cell at the end with five looks
  would settle it for about four seconds.
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
- [x] LTE band scan with a band selector, tuning to every channel in a
  coarse-to-fine order, and a headless `--lte-scan` so the results can be read
  without clicking. The LTE view borrows the receiver at 1.92 MS/s and gives
  the rate and the tuning back when it is left.
- [x] LTE analysis charts: the correlation profile, the candidate scores, the
  channel across the broadcast's subcarriers, and the elements themselves.
- [x] LTE, off the air: cell search to a physical cell identity, and the
  Master Information Block behind it -- bandwidth, PHICH configuration,
  antenna ports and a system frame number that keeps time. Reads cell 28 as a
  50-block 10 MHz carrier on two ports from `testfiles/lte_b20_pci28.bin` and
  from a live receiver. The part that was missing for a day: a phase
  measurement of a frequency offset only reports it modulo one subcarrier, and
  an uncalibrated dongle is two subcarriers out at 800 MHz.
- [x] Add GSM 900 ARFCN calibration with expected/measured waterfall markers,
  confidence reporting, stability gating, and PPM application.
