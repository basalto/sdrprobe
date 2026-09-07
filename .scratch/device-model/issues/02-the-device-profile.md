# 02 - `struct device_profile`: the data contract

Status: ready-for-agent

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
