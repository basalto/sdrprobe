# RTL-SDR Signal Probes

Small diagnostic tools that acquire and inspect raw RTL-SDR signals. This
context ends before modulation is interpreted as transmitted information; it
does not recover synchronization identities, content, or messages.

## Language

**Probe**:
A small diagnostic program used to verify receiver setup or inspect an acquired signal.
_Avoid_: Decoder, tracker

**Signal source**:
The origin of a raw sample stream, either a live RTL-SDR receiver or a recorded capture.
_Avoid_: Input device, backend

**RTL-SDR receiver**:
The radio hardware that tunes, samples, and supplies a live signal stream.
_Avoid_: Dongle, antenna

**Antenna**:
The physical receiving element whose placement and frequency response shape
what a receiving setup can observe.
_Avoid_: RTL-SDR receiver, LTE antenna port, signal source

**Capture**:
A recording of raw samples that can stand in for a live receiver during repeatable, hardware-free inspection.
_Avoid_: Fixture, decoded data

**I/Q pair**:
One in-phase sample and one quadrature sample representing a single instant in the acquired signal.
_Avoid_: Pixel, message bit

**Sample stream**:
An ordered sequence of interleaved I/Q pairs acquired from a signal source.
_Avoid_: Message stream, packet stream

**Sample block**:
A contiguous portion of a sample stream handled as one acquisition unit.
_Avoid_: Packet, message, frame

**Latest block**:
The single most-recent sample block handed from acquisition to rendering, overwritten rather than queued so the display always shows the freshest data.
_Avoid_: Queue, ring buffer, backlog

**Magnitude**:
The signal strength derived from an I/Q pair without retaining its phase.
_Avoid_: Power, amplitude byte

**Time bin**:
A contiguous interval of a sample block summarized into one plotted position.
_Avoid_: Frequency bin, sample

**Peak magnitude**:
The greatest magnitude within a time bin, preserving brief signal bursts that an average could hide.
_Avoid_: Average magnitude, decoded pulse

**Display frame**:
The set of time-bin peak magnitudes produced from one sample block for visualization.
_Avoid_: Sample block, video frame

**Magnitude view**:
A time-ordered visualization of peak magnitude that reveals brief changes in signal activity.
_Avoid_: Waveform

**Spectrum view**:
A frequency-ordered visualization of signal power across the receiver's sampled bandwidth.
_Avoid_: FFT view, 4 MHz spectrum

**I/Q scatter view**:
A plot of normalized full-scale in-phase values against quadrature values that reveals the distribution of acquired samples without implying decoded symbols.
_Avoid_: Constellation

**Waterfall view**:
A frequency-over-time visualization where each row is one spectrum, newer signal activity appears above older activity, and color represents digital signal power.
_Avoid_: Spectrogram history

**Noise floor**:
The baseline signal magnitude against which stronger activity is visually distinguished.
_Avoid_: Silence, zero signal

**Signal quality**:
Diagnostic measurements that compare recent signal activity with its noise floor and the receiver's remaining digital headroom.
_Avoid_: Reception quality, decode quality

**Signal activity**:
Signal energy observed above the noise floor that has not been demodulated or decoded by these probes.
_Avoid_: Message, transmission detection

**ADS-B activity**:
Signal activity observed using ADS-B-oriented tuning that may contain aircraft transmissions.
_Avoid_: ADS-B message, aircraft detection

### Gain and calibration

**Clipping**:
The fraction of I/Q pairs whose in-phase or quadrature sample reaches the digitizer's full-scale rail, indicating the gain is too high.
_Avoid_: Saturation warning, overflow

**Headroom**:
The remaining full-scale margin below the strongest recent sample, reported to guide gain selection before clipping begins.
_Avoid_: Gain margin, dynamic range

**Estimated SNR**:
A diagnostic ratio between a recent strong-signal percentile and the noise-floor percentile of one sample block, not a decoded link-quality figure.
_Avoid_: Link margin, decode confidence

**Peak hold**:
The retained maximum spectrum power at each frequency, decayed over time so brief bursts stay visible after they end.
_Avoid_: Max average, frozen trace

**DC-spike filter**:
Optional per-block removal of the mean I and Q offset so the receiver's center-frequency artifact does not dominate the spectrum and waterfall.
_Avoid_: High-pass filter, notch

**Frequency correction**:
A parts-per-million adjustment applied to the receiver's tuning to compensate for its sample-clock error.
_Avoid_: Tuning offset, drift fix

**Receiving site**:
The physical place where a receiver and antenna are used, naming the local
conditions and reference transmitters against which observations can be
compared.
_Avoid_: Receiver, antenna, survey name

**Receiver identity**:
The identity of one physical RTL-SDR receiver, whose crystal error must not be
assumed to match another receiver of the same model.
_Avoid_: Device index, signal source, receiving site

**Receiving setup**:
The combination of one receiver identity, receiving site, and antenna under
which survey observations can be compared meaningfully.
_Avoid_: Receiving site, signal source, calibration profile

**Calibration profile**:
A saved frequency correction for one receiver measured at one receiving site;
the receiver owns the crystal error, while the site records where and against
which available reference it was measured.
_Avoid_: Site correction, global PPM, calibration result

**Channel calibration**:
A guided procedure that measures a known transmitter's carrier to estimate and suggest a frequency correction.
_Avoid_: Auto-tune, alignment

**ARFCN**:
The GSM 900 downlink channel number (1-124) whose expected carrier frequency anchors channel calibration.
_Avoid_: Channel index, band slot

**EARFCN**:
The LTE downlink channel number whose carrier frequency the receiver is tuned to, on the standard's 100 kHz raster.
_Avoid_: LTE channel, ARFCN, band slot

**LTE sample grid**:
The 1.92 MS/s rate at which LTE's own arithmetic is integral -- 128 subcarriers of 15 kHz. Acquisition for LTE runs at it rather than at the 2 MS/s house rate, and the plugin refuses any other.
_Avoid_: LTE sample rate, resampled rate, native rate

**Measured carrier**:
The frequency estimated from an isolated calibration signal, compared against its expected frequency to derive a correction.
_Avoid_: Detected peak, tuned frequency

**Crystal error**:
The receiver oscillator's proportional frequency error, expressed in parts per million, that a frequency correction compensates; it varies with temperature and is specific to one device.
_Avoid_: Drift, PPM (the correction), clock skew

**BCCH**:
The GSM broadcast-control carrier; the continuously transmitted reference carrier that a calibration locks to, and the only one that carries an FCCH.
_Avoid_: Control channel, beacon

**FCCH**:
The GSM Frequency Correction Channel: an all-zeros burst that appears as a pure tone a fixed offset above its BCCH carrier, giving a precise, unbiased carrier reference.
_Avoid_: Sync burst, pilot

**Coherence**:
A zero-to-one measure of how tone-like a window of samples is, near one for a pure tone and near zero for modulation or noise; used as the confidence that a reference tone was found.
_Avoid_: Correlation, match score

**Channel power scan**:
A retuning sweep across a band that charts each channel's received power and flags the channels carrying a reference tone, to help choose a calibration channel. It walks a technology's channel grid; a sweep across an arbitrary frequency range is a band survey, which is a different thing.
_Avoid_: Spectrum sweep, band survey (that is the wideband one)

**Band survey**:
A retuning sweep across an operator-chosen frequency range that charts received power and marks where activity stands above the local noise floor, to find what is on the air before anything is known about it.
_Avoid_: Spectrum analyser, scanner, channel power scan

**Survey step**:
One retune in that sweep, contributing the usable middle of its sampled span; the outer edges are discarded because the tuner's response rolls off there.
_Avoid_: Sample block, channel, bin

**Signal candidate**:
A peak standing far enough above its local floor to be worth looking at, before anything is known about what it carries.
_Avoid_: Signal, transmitter, detection, station

**Survey carrier**:
One observed signal inferred by grouping candidate maxima that are not
separated by a sustained trough; one carrier may account for several
candidates.
_Avoid_: Signal candidate, station, transmitter, allocation

**Carrier extent**:
The frequency span of a survey carrier between the surrounding troughs that
separate it from neighbouring signals.
_Avoid_: Occupied bandwidth, channel width, allocation

**Carrier shape**:
A broad description of a survey carrier from its measured extent, such as
tone, narrow, medium, wide, or very wide; it describes appearance and does not
identify what the carrier is.
_Avoid_: Signal type, technology, allocation

**Confirmation pass**:
A closer observation that revisits survey claims with several independent
looks to determine whether the reported carrier is consistently present,
intermittent, or absent.
_Avoid_: Second sweep, decode, verification test

**Confirmation verdict**:
The result of a confirmation pass: confirmed when the closer observation
supports the survey claim, refuted when it contradicts the claim, or
intermittent when the carrier appears in only some looks regardless of the
claim.
_Avoid_: Detection, confidence, history status

**Receiving setup history**:
The accumulated record of survey carriers observed with one receiving setup,
including how often and when each has been present.
_Avoid_: Site history, survey archive, message log, capture collection

**History status**:
A long-term description derived from receiving setup history: new, steady,
intermittent, diurnal, or missing. It summarizes repeated surveys rather than
the several looks of one confirmation pass.
_Avoid_: Confirmation verdict, detection, availability

**Occupied bandwidth**:
The width of a candidate between the points where it falls a stated number of decibels below its peak, the drop being held clear of the noise floor and reported alongside the width.
_Avoid_: Channel width, baud, bitrate

**Receiver artifact**:
A candidate produced inside the receiver rather than received by it -- a harmonic of its reference clock, or its center-frequency offset landing in a step. Reported as a resemblance beside the measurement and never removed from the survey, because a real transmitter may sit on the same frequency.
_Avoid_: Spur, birdie, false positive, ghost, interference

**Reference comb**:
The regularly spaced set of tones a receiver's own clock leaves across the
band -- a comb in the ordinary sense of the word, as in a comb generator or an
optical frequency comb: spectral lines at a constant interval. Here the
interval is half the RTL2832U's 28.8 MHz reference, with a finer one at a
eighteenth. A frequency sitting on a multiple of either is evidence that a
candidate is a **receiver artifact**, never proof: the comb names where to be
suspicious, and the width and a closer look decide.
_Avoid_: Spur comb, clock noise, interference pattern, birdie spacing

**Comb spacing**:
The interval between adjacent tones of a reference comb, which is what the
`RECEIVER_COMB_SPACING_HZ` constants hold. Not the frequency of any one tone
-- a comb has no single frequency, which is the point of it.
_Avoid_: Comb frequency, harmonic frequency

**Band plan**:
A static table mapping frequency ranges to the service allocated there; it says what a frequency is *for*, never what a signal *is*, and a carrier found inside an allocation has not thereby been identified.
_Avoid_: Identification, classification, detection

**Power centroid**:
A power-weighted mean frequency across a channel, used as the fallback carrier estimate when no reference tone is present.
_Avoid_: Peak frequency, average

**Guard-band floor**:
A robust local noise reference taken from the band beside a channel, against which a carrier's strength is judged.
_Avoid_: Noise floor (global), baseline

**Prominence**:
How far a candidate carrier stands above its guard-band floor, in decibels; too small a prominence rejects the carrier.
_Avoid_: Peak height, amplitude

**Correction uncertainty**:
The standard error of the recent-residual center, in parts per million; the quantity a stable lock requires to fall below a threshold before the correction is trusted.
_Avoid_: Spread, standard deviation, error bar

### DSP architecture

**Generic SDR primitive**:
A technology-independent DSP operation on raw or centered I/Q, magnitudes, or dBFS spectra, reusable by any radio technology.
_Avoid_: GSM function, helper

**Technology DSP module**:
A self-contained, independently testable unit that interprets one radio
technology and reuses generic SDR primitives where they fit. Technology DSP
modules share dependency boundaries, not a uniform function interface.
_Avoid_: Runtime plugin, driver, backend, codec

**Channel map**:
A technology's rule for converting a channel number into its carrier frequency.
_Avoid_: Frequency table, ARFCN formula

**Reference tone**:
A known, tone-like feature a technology transmits that a DSP module detects to
identify and measure its carrier, such as the GSM FCCH.
_Avoid_: Pilot, beacon, sync word

**Calibration-grade detection**:
Identifying a reference carrier and measuring its frequency, as opposed to demodulating or decoding transmitted messages.
_Avoid_: Demodulation, decode

### Presentation

**SDR visual component**:
A reusable piece of the display that renders one kind of signal information (a plot, the waterfall, the scan chart, the health circle) from plain data and geometry, without knowing the application's state.
_Avoid_: Widget, panel

**Widget**:
A generic interface control with no signal meaning of its own, such as a button, checkbox, toggle, or text field.
_Avoid_: Component, view

**HUD composition**:
Assembling a screen from SDR visual components and widgets, keeping the arrangement separate from both the rendering of each piece and the application logic behind it.
_Avoid_: Layout engine, scene graph
