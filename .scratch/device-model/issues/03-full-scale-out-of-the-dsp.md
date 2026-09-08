# 03 - Full scale out of the DSP

Status: resolved, 2026-09-08. Full scale is the profile's everywhere; the
acceptance grep returns comments and `device_profile.h`'s own two format
functions. `check-sample-format` now runs the shipping converter on both
halves rather than a harness, which was this ticket's other criterion.

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


## What moved, and it was more than the five sites

The table above named five. The acceptance grep found four more, and they are
the reason the criterion was written as a grep rather than as a list:

| site | what it was | now |
| --- | --- | --- |
| `view_scope.c` scatter | `/ 127.5f` on I and Q | `/ app->device.full_scale` |
| `acquisition.h` | `PHYSICAL_MAGNITUDE_MAX 180.31223f` | `device_magnitude_max()` |
| `lte_dsp.c` | `full_scale = 127.5 * LTE_FFT_SIZE` | the profile's, threaded in |
| `acquisition.c` | the sidecar's format, in prose | format, `full_scale` and `bytes_per_pair` as fields |

`PHYSICAL_MAGNITUDE_MAX` is the clearest of them: 127.5 root two, written out
to five decimals, sitting between a block size and a scatter history depth. It
is a fact about the sample container and nothing about the display, and on a
12-bit part it is 2896.3.

## The decision that shapes everything downstream

**The floats stay in the device's own counts.** An RTL sample lands in
-127.5..+127.5 and a 12-bit part's in -2047.5..+2047.5; nothing is normalised
at the seam. That is what this ticket asked for and the reason is in its own
text: clipping means "at the ADC's rail", a rail is a count, and a 12-bit part
in a 16-bit word clips at 2047.5 and nowhere near 32767.5.

It has a consequence worth stating plainly, because ticket 01's expensive half
will meet it: **the 8-bit and 16-bit corpora do not produce the same floats**,
they produce floats sixteen times apart. What `check-sample-format` establishes
is that they are identical *after normalising each by its own full scale* --
which is exactly the comparison dBFS makes, and which every relative threshold
in the program inherits. An absolute threshold in counts would not inherit it.
Whether any decoder has one is a measurement nobody has taken yet.

## Threading it through LTE

`lte_reference_power()`, `lte_cell_search_all()` and `lte_channel_shape()` all
take `sample_full_scale` now, and only the first actually uses it -- for
`rsrp_dbfs` and `rssi_dbfs`, and nothing else. RSRQ, the RS-SINR, the cell
ranking inside `lte_cell_search_all` and the noise removal inside
`lte_channel_shape` are all ratios through one chain, so any fixed scale
cancels out of them. The other two carry the parameter only to hand it on.

That is thirteen call sites for two numbers, and it is worth it: leaving
`127.5` compiled into `lte_dsp.c` would make LTE the one technology whose dBFS
silently belonged to a receiver that was no longer attached.

## `fm_discriminate()`, which stays

Out of scope by this ticket's own last section, and it did not fall out for
free. It now names its offset -- `device_format_zero_offset(SAMPLE_FORMAT_U8)`
-- rather than spelling 127.5, and its comment says what it is: the last
raw-byte entry point, with no caller outside tests, because `view_fm.c` takes
`fm_discriminate_f()`.

## Acceptance

- `grep -n '127\.5' src/` returns comments plus `device_default_full_scale()`
  and `device_format_zero_offset()`, which are the profile's own definitions.
- `check-sample-format` runs the real converter on both containers: 68 checks,
  up from 64, with the harness deleted and four new ones asserting the
  converter counts pairs by `bytes_per_pair` and refuses a container nothing
  produces.
- `check-sdr-dsp`, `check-signal-probe` and `check-pipelines` unchanged.
  `make check` is **16995 checks in 45 suites** from clean.
- Eleven check rules gained `device_profile.h` as a prerequisite, and it is in
  `DSP_HDR` -- `sdr_dsp.h`, `lte_dsp.h` and `fm_dsp.h` all include it now, so
  it is a DSP-layer header and not only an app one.
- Looked at: magnitude, scatter, spectrum and LTE all draw, and the LTE capture
  still reads cell 28 with 2 ports.
