# 05 - Does the envelope carry anything, and does the frequency sit on levels

Status: resolved

Blocked on nothing, though it reads better after 04: a burst is the right
window to measure these in, and measuring them across the idle gaps averages
in the noise.

## Two numbers, and what each separates

**Envelope variation** -- the coefficient of variation of the magnitude, or
equivalently peak-to-average power. It splits everything that keeps a constant
envelope from everything that does not, without naming either:

- near zero: a bare carrier (`carrier_power_fraction` already says this)
- low: FM, FSK, GMSK, and any constant-envelope phase modulation
- moderate: a filtered single carrier -- root-raised-cosine PSK or QAM
- high, near Gaussian: OFDM, or noise

**The instantaneous-frequency histogram** -- the derivative of phase, binned.
It answers a question nothing here asks: is the frequency sitting on a small
number of levels? Multi-modal means FSK, and **the spacing between the modes
is the deviation**, which is a parameter and not a guess. Unimodal and wide is
FM or something phase-modulated; uniform is noise.

Together they are two of the nine features the reference chain lists under
extraction, and they are the cheapest two remaining: both are one pass over
samples this program already has in memory.

## What the captures should give

Every one of these is already in `testfiles/` and every one has a different
expected answer, which is what makes the pair worth measuring rather than
asserting:

| capture | envelope variation | frequency histogram |
| --- | --- | --- |
| a bare carrier at 75.000 MHz | near zero | one spike |
| `fm_rds_tsf.bin` | low -- FM is constant envelope | unimodal, wide |
| `tetra_cc17.bin` | moderate -- pi/4-DQPSK through a root-raised-cosine filter | four levels, if the timing is right |
| `lte_b20_pci28.bin` | high -- OFDM | broad |
| `adsb_cpr_pair.bin` | very high -- on-off keying, mostly off | meaningless, and must be refused |

The last row is the point of writing the table: an on-off keyed signal is
*mostly noise by sample count*, so its instantaneous frequency is the noise's.
The measurement has to refuse, not average.

## Where it refuses

Two gates, and both have to be there:

- **Below `SIGNAL_CARRIER_PRESENT_DB` there is nothing to measure.** The
  instantaneous frequency of noise is uniformly distributed and perfectly
  well defined, so a weak signal produces a confident histogram of the
  receiver. This is the same mistake `carrier_power_fraction` made on its
  first run and the same fix.
- **A signal far down the ADC's range is measuring the ADC.** Eight bits is
  about 45 dB, and an envelope variation computed on a signal 40 dB below full
  scale is quantisation. State the minimum level and refuse under it.

## What this must not become

The spec's closing line applies here more than anywhere: these narrow what a
reader should try next, they do not conclude. "Constant envelope, four
frequency levels 4.5 kHz apart" is a measurement. "FSK" is an identification,
and this program's rule is that an identification comes from decoding
something the transmitter said.

Specifically out of scope, and worth naming so nobody tries: QAM order needs
carrier and timing recovery at an SNR a telescopic whip does not give; PSK
order past QPSK needs the eighth power and pays for it in noise; DSSS needs
spectral correlation over seconds of contiguous samples; FHSS hops out of the
2 MS/s window and returns fragments indistinguishable from bursts.

## Comments

**2026-09-06 — done, with the second half not built and the reason measured.**

`signal_envelope_stats()` in `signal_probe`, on the channel mixed to zero and
filtered to its own width. That isolation is the whole difficulty and it is
the trap ticket 02 fell into: measured across a 2 MHz capture, the envelope of
any narrow signal is the envelope of the noise beside it.

Measured across every capture, in a channel isolated to its own width:

| signal | variation | peak/mean |
| --- | --- | --- |
| FM broadcast, continuous | **0.032** | 2.2 dB |
| a bare carrier, 5 kHz channel | 0.137 | 3.8 dB |
| a bare carrier, 20 kHz channel | 0.248 | 6.3 dB |
| TETRA, pi/4-DQPSK | 0.252, 0.272 | 4.3, 4.9 dB |
| GSM at one carrier | 0.291 | 5.0 dB |
| **an empty channel** | **0.545, 0.554** | 11.0, 11.2 dB |
| GSM at another | 0.792 | 9.7 dB |
| Mode S, pulsed | 1.057 | 26.0 dB |
| an LTE downlink, OFDM | 1.098 | 18.8 dB |

Complex Gaussian noise has a Rayleigh magnitude whose coefficient of variation
is `sqrt(4/pi - 1) = 0.5227` and depends on nothing at all, which is why the
empty channels land there and why it is the reference every reading is quoted
against. It is also what OFDM reads, for the same reason its peak-to-average
is punishing: a sum of many independent subcarriers is Gaussian.

So 0.30 sits under everything whose envelope is contained and a factor of 1.8
under an empty channel; 0.90 sits over the two that vary more than noise does.

On air the FM branch reads **envelope hardly varies: 0.02, noise 0.52**.

### Two limits, both measured, both said on screen or in the header

**It is a scale, not a classifier.** A bare carrier in noise reads 0.248 and
filtered pi/4-DQPSK reads 0.252 -- the same number. "Contained" is true of
both and separates neither; `carrier_power_fraction` is what tells those apart,
and the sentence is worded so it does not appear to be doing more than it is.
The band between the two thresholds is left unnamed entirely, because that is
where an empty channel sits.

**It measures the envelope over the look, not the envelope of the
modulation.** GSM is GMSK, constant-envelope by construction, and reads 0.29
to 0.79 -- because it is also TDMA and the envelope goes to nothing between
timeslots. A low reading means constant envelope only for something that does
not stop, which is why it is printed after the burst lines rather than before
them.

### The instantaneous-frequency histogram was not built

The ticket wanted modes counted and their spacing read as an FSK deviation.
The spread measured first says it will not work here:

| channel | signal spread | channel width |
| --- | --- | --- |
| TETRA, 25 kHz | 5.0 kHz | 25 kHz |
| **an empty 25 kHz channel** | **8.7 kHz** | 25 kHz |
| FM, 180 kHz | 16.7 kHz | 180 kHz |

The noise is *wider* than the signal in the same channel, so a histogram of a
real capture at these levels is mostly the noise's and its modes are the
noise's. It needs a signal-to-noise this receiver and this antenna do not give
on a narrow channel, and building the mode counter would have produced
confident spacings from it -- the failure mode of ticket 02's symbol-rate
search, which was at least caught before shipping.

`frequency_spread_hz` and `mean_frequency_hz` are reported because they are
honest measurements -- the residual offset in particular says how far out the
carrier search was -- and nothing is concluded from either.

### Out of scope, still

QAM order, PSK past QPSK, DSSS and FHSS remain out of reach for the reasons
this ticket already listed, and the frequency-spread table above is now
evidence for the first two rather than an argument.
