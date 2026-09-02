# LTE runs on LTE's sample grid, not the house rate

## Status

accepted

## Context

Every other path in this program samples at 2 MS/s. It is written into the
conventions (one sample = 0.5 µs, block size 16 × 16384), it is what the GSM
and Mode S captures in `testfiles/` were recorded at, and it is what dump1090
uses, which is where the figure came from.

LTE does not fit on it. An LTE carrier is a grid of 15 kHz subcarriers, and
every count in the standard — the FFT size, the cyclic-prefix lengths, the slot
and frame durations — is an integer only at a sample rate that is a multiple of
that spacing. The smallest one is 128 × 15 kHz = 1.92 MS/s exactly, at which a
slot is 960 samples, a frame is 19200, and a symbol is 128 samples of useful
part behind a prefix of 9 or 10. At 2 MS/s none of those is an integer.

Two ways out. Resample 2 MS/s to 1.92 in the plugin — a rational 24/25
polyphase filter, about a hundred lines, run over every block before anything
else. Or let LTE acquire at its own rate.

## Decision

**LTE acquires at 1.92 MS/s.** `--earfcn` sets it, `--technology lte` sets it,
and a `--sample-rate` that disagrees is rejected rather than overridden. The
plugin's entry points take a sample rate and refuse anything that is not
1.92 MS/s, rather than resampling quietly.

An RTL-SDR reaches 1.92 MS/s exactly: the RTL2832U divides a 28.8 MHz clock,
and 28.8 / 1.92 = 15. Nothing is approximated.

`--earfcn` also differs from `--arfcn` in where it tunes. A GSM channel is
tuned 400 kHz below its carrier, to keep the carrier off the receiver's DC
spike. An LTE carrier is tuned to its centre, because LTE never transmits on
its middle subcarrier: the DC spike lands in a hole the standard already
leaves, and the synchronisation signals sit either side of it.

## Consequences

- **An LTE capture is not interchangeable with the others.** Its sidecar
  records 1920000 and the plugin checks it. Feeding an LTE capture to the
  GSM path, or a 2 MS/s capture to the LTE path, is refused rather than
  producing a plausible wrong answer.
- **The LTE view cannot be reached by switching to it mid-run.** The settings
  panel has no sample-rate field, so a receiver started at 2 MS/s stays there;
  the view says so and names the flag to restart with. Adding a rate to the
  settings panel would fix that and is a separate change.
- **No resampler exists to check.** The alternative would have put a filter
  ahead of every correlation, with its own passband, its own group delay and
  its own tests, in service of a rate LTE does not use.
- **What the rate can see is bounded, and the bound is the standard's.** The
  primary and secondary synchronisation signals and the broadcast channel all
  live in the central 1.08 MHz of a carrier whatever its real bandwidth,
  precisely so a handset can find a cell before it knows how wide it is.
  Everything above the Master Information Block — SIB1 and the rest — is
  scheduled across the full bandwidth, 9 MHz for a typical band 20 carrier, and
  is simply not in these samples. That is a property of the hardware and the
  standard together, not a missing function.
