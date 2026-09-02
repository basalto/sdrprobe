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

- [ ] Read RDS off FM broadcast. The full-range sweep puts FM 14 dB above
  everything else here, the 19 kHz pilot sits 32 dB over the noise and the
  57 kHz band 10 dB over it, and the payload -- programme identification,
  station name, radio text -- is the same shape as GSM's BCCH and LTE's Master
  Information Block. It is also the only strong thing in the sweep that fits
  the receiver with room to spare. `.scratch/rds-decoder/`.
- [ ] Name the frequencies the band plan cannot. 88 of the sweep's candidates
  fall in gaps in the table, a third of everything heard, including
  416.652 MHz at -23.9 dBFS -- the loudest thing that is not broadcast radio.
  A survey's allocation is the only claim it is allowed to make about identity
  (ADR-0015), so a gap is the survey losing the one thing it can honestly say.
  `.scratch/band-plan-coverage/`.
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
- [x] Surveys that accumulate. `surveys/` keeps one JSON per sweep,
  `scripts/survey_tool.py` ingests, reports and diffs them, and the
  `rf-environment` skill reads them for what is known, what is new, and
  whether a candidate technology passes the two gates -- does it fit the
  receiver, and is it actually there. Both gates exist because something
  failed them: 5G NR after a ticket was written for it, DAB+ just before one
  was.
- [x] Told what is transmitting on n28, which turned out to close the question
  rather than open a decoder. The 700 MHz downlink here carries 5G NR at
  30 kHz subcarrier spacing: a burst every 20 ms at 8.1 times its floor with
  nothing at all at 5 ms, and a cyclic prefix that correlates at 64 samples
  rather than 128. At 30 kHz a synchronisation block's 127 subcarriers span
  3.81 MHz and an RTL-SDR reaches about 2.4 MS/s, so not even a cell identity
  is within reach and `.scratch/nr-cell-search/` is closed. `make
  probe-periodicity` is what survives: it separates LTE from NR and names the
  grid, on a signal nothing here can demodulate.
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
