# RTL-SDR Signal Probes

Small diagnostic tools that acquire and inspect raw RTL-SDR signals, using ADS-B-oriented settings by default. This context ends at signal statistics and visualization; it does not demodulate or decode transmitted messages.

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

**Channel calibration**:
A guided procedure that measures a known transmitter's carrier to estimate and suggest a frequency correction.
_Avoid_: Auto-tune, alignment

**ARFCN**:
The GSM 900 downlink channel number (1-124) whose expected carrier frequency anchors channel calibration.
_Avoid_: Channel index, band slot

**Measured carrier**:
The frequency estimated from an isolated calibration signal, compared against its expected frequency to derive a correction.
_Avoid_: Detected peak, tuned frequency

### DSP architecture

**Generic SDR primitive**:
A technology-independent DSP operation on raw or centered I/Q, magnitudes, or dBFS spectra, reusable by any radio technology.
_Avoid_: GSM function, helper

**Technology plugin**:
A small, testable DSP module for one radio technology that supplies a channel map and a reference-tone detector and reuses the generic SDR primitives for everything else.
_Avoid_: Driver, backend, codec

**Channel map**:
A technology's rule for converting a channel number into its carrier frequency.
_Avoid_: Frequency table, ARFCN formula

**Reference tone**:
A known, tone-like feature a technology transmits that a plugin detects to identify and measure its carrier, such as the GSM FCCH.
_Avoid_: Pilot, beacon, sync word

**Calibration-grade detection**:
Identifying a reference carrier and measuring its frequency, as opposed to demodulating or decoding transmitted messages.
_Avoid_: Demodulation, decode
