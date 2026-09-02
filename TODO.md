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

- [ ] Tell what is transmitting on n28. Band 28 is occupied here -- a survey
  found ten candidates across 758-788 MHz, the strongest at 774.2 MHz -- and
  `--lte-scan 28` finds no LTE cell in any of it, which is consistent with 5G
  and equally consistent with a weak LTE carrier. Nothing in this program can
  currently tell those apart. An NR synchronisation detector would, as far as a
  cell identity: 127 subcarriers at 15 kHz is 1.905 MHz and fits the 1.92 MS/s
  grid, while the 240-subcarrier broadcast block does not fit any rate this
  dongle has. See `.scratch/nr-cell-search/`, whose first ticket is whether
  n28's subcarrier spacing makes even the identity reachable.
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
- [x] An LTE band scan that can be believed either way. The sweep takes three
  looks at every channel rather than two, because a cell that answers in half
  its blocks is silent through two looks a quarter of the time -- the live
  band 20 carrier at 816 MHz is the one that taught this. A confirmation pass
  then revisits each listed cell with five more looks and drops any that
  cannot repeat its identity, which costs about four seconds for a handful of
  cells and is what lets the sweep stay generous. `--lte-scan` reports the
  drops alongside the finds.
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
