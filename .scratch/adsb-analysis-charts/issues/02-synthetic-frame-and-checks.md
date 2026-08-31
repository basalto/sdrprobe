# 02 — Synthetic Mode S frame, and DSP checks for the trace

Status: resolved
Blocked by: 01

`tests/adsb_dsp_test.c` decodes hand-written byte arrays today: it never runs
the demodulator over samples, so nothing pins `preamble_at()`, the bit slicer,
or the new trace. Give it a signal to work on, the way `gsm_sch_modulate()`
does for GSM.

## Modulator

```c
/* Write a Mode S frame into a magnitude buffer as the receiver would see it:
   the four-pulse preamble then one pulse per bit, at ADSB_SAMPLES_PER_BIT
   samples per bit, amplitude `high` over a `noise` floor. Returns samples
   written. Exposed so the checks demodulate a signal they built, rather than
   asserting against a buffer this file also produced. */
size_t adsb_modulate_frame(const uint8_t *bytes, int byte_count,
                           float high, float noise,
                           float *magnitudes, size_t start, size_t capacity);
```

## Checks to add

- **Round trip.** Modulate a known DF17 frame, `adsb_demod()` it, get one
  message back with the right ICAO and callsign.
- **Landscape peak.** `trace.landscape[trace.landscape_center]` is the maximum,
  and stands at least 2x above the median of the landscape.
- **Confidence.** On a clean frame every `confidence[i] > 0.8`. Re-modulate with
  the two half-intervals pushed close together and confidence collapses toward
  0 while the bits still decode — that is the marginal-frame case the chart
  exists to show.
- **Failure is latched.** Corrupt one byte after modulating: `adsb_demod()`
  returns 0 messages, and the trace still comes back `valid` with `crc_ok == 0`.
- **Envelope.** `envelope[0]`, `[2]`, `[7]`, `[9]` are all near 1.0 after
  normalisation, and samples 10..15 are near the noise floor.

Extend `scripts/adsb_chain_probe.c` to print the trace for a real capture
(`make probe-adsb-chain`), and use it to re-pick the ±32 landscape width
against `testfiles/adsb_modes1.bin` — spec open question 2.

## Comments

**Landscape width settled at +/-32** (spec open question 2), measured against
`testfiles/adsb_modes1.bin` with `make probe-adsb-chain`: on the last real frame
in the capture the peak scores 3.51 against a runner-up of 1.72 and a field mean
of 0.93, so the lock stands about 2x clear of its nearest rival inside the
window. Narrower would have cropped the field the peak is judged against;
wider makes it a spike.

**Real-signal confidence is nothing like the synthetic frame's.** The checks
assert `> 0.8` on a modulated frame, which is a noiseless best case; the same
figure over the capture is a mean of 0.411 with a worst bit of 0.050. That is
the spread the bar chart exists to show, and it is why the check thresholds are
written against the synthetic signal only.

**The decode funnel over the whole capture**: 382 preambles accepted, 115
squitter-shaped, 11 CRC failures, 104 decoded. Two thirds of accepted preambles
are not squitters at all, which is worth knowing before anyone reads the first
counter as a frame count.
