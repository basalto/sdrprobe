# 05 - Does the envelope carry anything, and does the frequency sit on levels

Status: needs-triage

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
