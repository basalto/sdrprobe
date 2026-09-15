# 05 - signal_report and ook_report each load samples their own way

Status: done

## What

`scripts/signal_report.c` and `scripts/ook_report.c` both carry a private
byte-to-float conversion:

    i[k] = ((float)raw[2 * k]     - 127.5f) / 127.5f;
    q[k] = ((float)raw[2 * k + 1] - 127.5f) / 127.5f;

Every other probe in `scripts/` -- `gsm_chain_probe.c`, `adsb_chain_probe.c`,
`dsp_bench.c` -- calls `sdr_dsp_convert_iq()` with a `struct device_profile`
instead. That function's header calls itself **"the one byte-to-float seam"**,
and CLAUDE.md's device-model section rests a whole argument on there being
exactly one: identical floats downstream means identical answers *by
construction rather than by measurement*, which is why no capture is decoded
twice to establish the 8-bit/16-bit equivalence.

These two files are outside that argument. They are also outside it in a second
way: they **normalise by 127.5** where the seam deliberately does not, because
"the floats stay in the device's own counts" -- clipping means at the ADC's
rail and a rail is a count, which is the whole reason a 12-bit part's 2047.5 is
not its container's 32767.5.

## Why it does not currently produce a wrong answer

Everything both tools compute is a ratio: `carrier_over_noise_db` is a line
over a median probe, `contrast_db` is one percentile over another,
`variation` is a coefficient of variation, `peak_over_mean_db` is in its name,
and `carrier_power_fraction` is a fraction. A constant scale factor cancels out
of all of them, so the divergence is currently invisible.

**That is exactly the condition under which this kind of thing survives to bite
later.** The first statistic either tool grows that is *absolute* -- a dBFS
level, a clipping count, a headroom figure -- reads wrong by 42 dB with nothing
on screen to say so, and it will read wrong only in the diagnostic, which is
the tool a reader reaches for to find out why.

It also means neither tool can read a 16-bit capture at all, while the built
program now reads both corpora.

## Why it happened, which is the part worth keeping

`ook_report.c` was written by copying the house pattern from its nearest
neighbour, `signal_report.c`, and the neighbour was already off-convention. A
convention with one exception acquires a second by being copied, and the copy
looks like conformance to whoever reads the two side by side.

## Proposed

Move both to `sdr_dsp_convert_iq()` with `device_profile_rtlsdr()`, taking the
container from `capture_sidecar.h` the way `check-pipelines`' wide-container
group does, so both probes read a 16-bit capture. Then re-run
`make probe-signal` over `testfiles/carrier_75000_bare.bin` and `make probe-ook`
over both SRD remote control captures and assert the printed figures are **unchanged** --
they should be, because the statistics are ratios, and if any of them moves
that is the finding rather than the refactor.

The thresholds are the thing to check rather than assume:
`SIGNAL_BARE_FRACTION` and `SIGNAL_CARRIER_PRESENT_DB` are compared against
ratios and should not care, but `ook_report.c`'s own `FLOOR_OOK` default of
25 dB is a dB over a median and also should not. "Should not" is the claim to
measure.

## Not in scope

Whether `probe-signal` should gain a time axis -- that is ticket 01, and it is
a separate question. This one is about how both tools get their floats,
whatever window they ask for.


## Done

Both reporters go through `sdr_dsp_convert_iq()`. `probe-signal` now links
`src/sdr_dsp.c`; `probe-ook` already did.

**Verified by diffing the whole report across the change**, which is the only
thing that could have settled it, and it caught two faults that reasoning had
already waved through.

### `probe-ook`, both SRD remote control captures

One line moved and it is the only absolute number printed:

    -  Envelope, as a fraction of the window's peak (1.1614):
    +  Envelope, as a fraction of the window's peak (148.0764 counts):

1.1614 x 127.5 = 148.08, and 1.1226 x 127.5 = 143.13 on the other capture.
Every transmission, every dB, every histogram bucket and every run length is
byte-identical, because each is a ratio. The label now names the units.

### `probe-signal`, `carrier_75000_bare.bin`

The target line is unchanged. Both **controls** stopped refusing to measure
their envelope -- which is not noise in the diff, it is ticket 06: the
refusal's threshold is in normalised units the program has not supplied since
`.scratch/device-model/`, so the diagnostic was refusing where the program
measures. They agree now.

### Two faults on the way, both silent

1. **`sdr_dsp_convert_iq()` requires `magnitude_out`** -- a NULL returns 0
   pairs rather than skipping the work. `probe-ook` reported *"No transmission
   stands 25 dB over the floor anywhere in this capture"* on a capture holding
   four, which is a plausible answer and not a crash. It keeps a scratch
   buffer it never reads, and checks the return.
2. **`sdr_dsp_spectrum()` takes a `full_scale`** and it had been left at
   `1.0f`. With samples in counts every bin reads about 42 dB high, peak and
   median converge, and the same "nothing anywhere" message appears for an
   entirely different reason. It is the profile's now.

Both produce *confident empty reports*, and a tool whose job is to say whether
a band is empty has no worse failure mode. Neither is reachable by any check
-- no `probe-*` target is built by `make check`, which CLAUDE.md already
records as having silently broken four of them.

### Gate

`make check-signal-probe` 150 checks ok. Full `make check` re-run after the
change.
