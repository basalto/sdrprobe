# 03 - Full scale out of the DSP

Status: ready-for-agent
Blocked by: 01, 02

`127.5` is the definition of dBFS in four places and the display ceiling
assumes it in a fifth:

| site | what it does |
| --- | --- |
| `sdr_dsp.c:82-83` | byte to centred float |
| `sdr_dsp.c:229` | clipping, at `>= 127.5` |
| `sdr_dsp.c:262` | headroom, in dB below full scale |
| `sdr_dsp.c:707` | the FFT's scale factor |
| `acquisition.h:69` | `SPECTRUM_TOP_DBFS 6.0f` |

## What to build

`sdr_dsp_convert_iq()` takes a `const struct device_profile *` and reads
format, full scale and bytes per pair from it. The three consumers of full
scale inside `sdr_dsp.c` read it from the same place. `SPECTRUM_TOP_DBFS`
stays a display constant but is documented as dB relative to whatever the
profile says full scale is, which is what it already meant.

Clipping must keep meaning what it means: samples at the ADC's rail. For
12-in-16 that is `>= 2047.5` and not `>= 32767.5`, which is why `full_scale`
is the ADC's and not the container's.

## Acceptance criteria

- `make check-sample-format` (ticket 01) passes with the real format layer
  rather than a test harness.
- `check-sdr-dsp`, `check-signal-probe` and `check-pipelines` unchanged.
- `grep -n '127\.5' src/` returns only the RTL profile constructor and comments.

## Not in scope

- `fm_discriminate()`'s raw-byte signature. It is the last such entry point;
  convert it in this ticket only if it falls out for free, otherwise its own.
