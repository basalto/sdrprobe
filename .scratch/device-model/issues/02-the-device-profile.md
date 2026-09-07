# 02 - `struct device_profile`: the data contract

Status: resolved, 2026-09-08. `src/device_profile.h` and
`check-device-profile`, 82 checks. Nothing reads it yet, which is the point --
tickets 03 to 06 move one area each.

The facts a receiver has that this program's numbers depend on, in one
raylib-free, librtlsdr-free header, so a check can construct one without a
device and a capture can carry one.

## What to build

`src/device_profile.h`:

- `enum sample_format` -- `SAMPLE_FORMAT_U8` and `SAMPLE_FORMAT_S16`, room for
  `CF32`. The enum is the discriminator; nothing else switches on the device.
- `struct device_profile` holding: format; `full_scale` (127.5 for u8, 2047.5
  for a real 12-in-16 part -- but **2040.0 for a capture rescaled by ticket
  01**, which is 127.5 x 16 and not the same number; see that ticket's
  Finding 1, where 2047.5 agreed with none of the 256 byte values);
  `bytes_per_pair`; `tune_lower_hz` / `tune_upper_hz`;
  `rate_min_hz` / `rate_max_hz`; `can_retune`; a gain model
  (`GAIN_MODEL_LIST` with the list, or `GAIN_MODEL_RANGE` with min/max/step,
  and a unit); `has_ppm_correction` and `ppm_drifts`; `reference_clock_hz`;
  `settle_seconds`; a short `name`.
- Two constructors: `device_profile_rtlsdr(...)` filling today's values, and
  `device_profile_capture(...)` for file playback -- same format, `can_retune`
  false, which is what the code already enforces.
- Derived accessors, because these are the arithmetic the rest of the program
  gets wrong by hand: `device_pairs_per_block(profile, block_bytes)` and
  `device_block_seconds(profile, block_bytes, rate_hz)`.

`tests/device_profile_test.c` and `check-device-profile`, in `CHECK_UNITS`,
with `device_profile.h` in `APP_HDR`.

The 2040/2047.5 split is the reason `full_scale` belongs in the profile at all
rather than being derived from the format: two sources with the same
`SAMPLE_FORMAT_S16` can have different full scales, and only the source knows
which. `device_profile_capture()` must take it as an argument and read it from
the sidecar's `full_scale` field, which `rescale_capture` already writes.

Checks: the RTL profile reproduces today's constants exactly -- full scale
127.5, two bytes a pair, 131072 pairs a block, 65.5 ms at 2 MS/s, 24 to
1766 MHz; a 12-in-16 profile at the same block size gives 65536 pairs and half
the seconds, which is the arithmetic ticket 04 exists to stop being silent; a
capture profile refuses retuning; and a gain list and a gain range are both
representable and distinguishable.

## Decisions

- **Data, not a vtable.** No function pointers until ticket 07 has a second
  backend to satisfy them. The data is what the checks need and it can land
  alone.
- The profile does not know about `struct app`, raylib, or any driver header.
- `full_scale` is a float and is the definition of dBFS. Nothing else may
  hardcode one.
- One profile per source. When both a capture and a device are open, they are
  two profiles; nothing is global.

## Acceptance criteria

- `make check-device-profile` passes and is gated.
- The header includes no driver and no GUI header.
- No behaviour changes: nothing reads the profile yet.

## Not in scope

- Making anything use it. Tickets 03 to 06 do that, one area each.


## What was built

`src/device_profile.h`, header-only and dependency-free -- `<stddef.h>` and
`<string.h>` and nothing else. `enum sample_format` (U8, S16, room for CF32),
`enum gain_model` (NONE / LIST / RANGE) with a unit, `struct device_profile`,
the two constructors, the two derived accessors, plus three that fell out of
writing the checks:

- `device_default_full_scale()`, which **returns 0 for S16** -- see below;
- `device_format_bytes_per_pair()`, so the width is derived in one place;
- `device_profile_valid()`, which catches the incoherent structs a hand-filled
  one produces: a format at odds with its own width, no full scale, either
  range the wrong way round, a negative settle, a list model with no list, a
  range model with no range.

The gain list is **embedded rather than a pointer**. `app.h` holds
`int *supported_gains` because that is what librtlsdr hands back, but a profile
is a value that gets copied and kept per source, and a borrowed pointer inside
one is a lifetime question nobody needs. `device_profile_set_gain_list()`
**refuses** a list that does not fit rather than truncating: a settings panel
silently one entry short is a gain nobody can select and nothing that says so.

## The full-scale decision, restated because it is the reason for the module

Ticket 01 measured it and this header encodes it. The rescaled corpus and a
real 12-bit part are **both `SAMPLE_FORMAT_S16`** and their full scales are
2040.0 and 2047.5. So:

- `full_scale` is a field, not a function of `format`;
- `device_profile_capture()` takes it as an argument, from the sidecar's
  `full_scale` field that `rescale_capture` already writes;
- `device_default_full_scale(SAMPLE_FORMAT_S16)` returns **0**, not a guess. A
  caller that gets 0 has to go and find out. A default there would have handed
  2047.5 to the corpus that agrees with it on none of its 256 byte values.

`test_two_s16_sources_disagree_about_full_scale` is that argument as a check.

## Pinned against the originals, not restated

The suite includes `survey_bands.h`, `survey_sweep.h` and `survey_suspect.h`
and asserts the profile equals `SURVEY_TUNER_LOWER_HZ` / `_UPPER_HZ`,
`SURVEY_SETTLE_SECONDS` and `RECEIVER_REFERENCE_HZ` -- and that the comb
spacing is still that reference halved. A check quoting 24 MHz on both sides
would pass while the program disagreed with itself. Verified by mutation:
moving the profile's upper reach to 1700 MHz fails two checks by name.

`acquisition.h` is the one it cannot include -- `<rtl-sdr.h>`, and a check here
links `-lm` only -- so `SAMPLE_BLOCK_BYTES` is written out with the same
comment `sample_format_test.c` carries.

## What the profile deliberately cannot say

**librtlsdr's rate range has a hole in it**: 225001-300000 and 900001-3200000
Hz, with nothing between. `rate_min_hz` / `rate_max_hz` cannot express that,
and inventing a representation for one device's quirk is what ticket 07 exists
to force honestly. Nothing here tunes into the gap -- 2.0, 2.048 and 1.92 MS/s
are all in the upper span -- so it is a documented limitation in the
constructor rather than a field.

A capture's profile carries **no reference clock and no ppm correction**, and
neither is an omission. Whichever device recorded it had a clock, but the file
does not, so nothing downstream may attribute a comb to it; and whatever
frequency error the receiver had is already baked into the samples, so there is
nothing left to correct. Its tuning range is the **single frequency** it was
taken at -- not zero, and not the tuner's range -- because being at one place
on the band is a fact about a capture, not a missing capability.

## Verified by mutation

Three deliberate breakages, each firing by name: the profile drifting from
`SURVEY_TUNER_UPPER_HZ`; `device_format_bytes_per_pair()` returning 2 for S16
(four checks, including the block arithmetic); and S16 acquiring a default full
scale of 2047.5.

## Acceptance

- `make check-device-profile` passes and is in `CHECK_UNITS`; `device_profile.h`
  is in `APP_HDR`. Both audits clean.
- The header includes no driver and no GUI header.
- No behaviour changes -- nothing reads it. `make check` is 16991 checks in 45
  suites from clean, `check-pipelines` included.
