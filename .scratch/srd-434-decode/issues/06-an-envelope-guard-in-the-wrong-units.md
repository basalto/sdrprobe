# 06 — an envelope guard measured in units nothing supplies

Status: done

## What

`SIGNAL_ENVELOPE_MIN_RMS` is `0.002` (`src/signal_probe.h:559`), and
`signal_envelope_stats()` refuses to measure below it with the comment
*"measuring the quantiser, not the modulation"*. Its stated basis is the
container's range:

> Below this level there is nothing to measure: eight bits is about 45 dB, so
> a variation computed on a signal far down the range is measuring the
> quantiser rather than the modulation.

That reasoning is about a **fraction of full scale**. The constant is written
as though the samples were normalised to unity — and since
`.scratch/device-model/`, they are not. `sdr_dsp_convert_iq()` delivers the
device's own counts, and CLAUDE.md is explicit that this is deliberate:

> The floats stay in the device's own counts and are deliberately not
> normalised, because clipping means "at the ADC's rail" and a rail is a count.

So the guard is in units nothing supplies, and it is **dead in the shipping
program**.

## Measured

RMS of the channel the program actually hands it, over 400000 pairs, against
the 0.002 threshold:

| capture | RMS in counts | fraction of full scale | clear by |
| --- | --- | --- | --- |
| `carrier_75000_bare.bin` | 2.448 | 0.0192 | 1224x |
| `adsb_modes1.bin` | 24.882 | 0.1952 | 12441x |

An 8-bit container cannot produce an RMS of 0.002 counts at all: one least
significant bit is 1.0. The branch is unreachable from any real capture.

## How it was found

Ticket 05 put `scripts/signal_report.c` through `sdr_dsp_convert_iq()` instead
of its own hand-rolled `(byte - 127.5) / 127.5`. Diffing the whole report
across that change, the target line was unchanged and **both control
frequencies stopped refusing**:

```
-   envelope shape:   refused, too far down the range to measure
+   envelope shape:   variation 0.627 (noise reads 0.523)  peak/mean 14.4 dB
```

That is the seam bug making itself visible. Normalised, a weak control sits
near 0.002 and the guard fires; in counts it is a thousand-fold clear and the
guard never fires. **The diagnostic and the program disagreed about when a
channel is too weak to measure**, which is exactly the class of divergence the
one-seam rule exists to prevent, and neither was checked against the other.

The diagnostic now agrees with the program. Nothing is left that exercises the
refusal, in either.

## What to do

Make the threshold what its own comment says it is: a fraction of full scale.

`signal_envelope_stats()` has no profile and no full scale. There are 14 call
sites across `src/survey_session.c`, both `scripts/` reporters and
`tests/signal_probe_test.c`; `make add-argument` exists for precisely this
shape of change and `check-add-argument` covers its traps. Passing a
`double full_scale` is lighter than a whole profile and is what the one
comparison needs.

Then `0.002 * full_scale` is 0.255 counts on an 8-bit container and 4.1 on a
12-bit one, which is the same claim about the quantiser in both — and is the
`device_profile` argument in its ordinary form.

## Refusals

- **Do not simply delete the guard.** It is unreachable today, but a 12-bit
  container and a quiet channel is the case it was written for, and that
  device is the whole subject of `.scratch/device-model/`.
- **Do not rescale the constant to counts.** 0.255 would be right for this
  dongle and wrong for the next one, which is the fault
  `device_default_full_scale()` refuses to commit by returning 0 for S16.
- The check to add is the one that does not exist in either direction: a
  buffer scaled down until the refusal fires, asserted to fire at the same
  *fraction* in both containers. A test that pins the counts reproduces the
  bug.

## Comments

Found while verifying ticket 05 rather than by any check. The suite passed
throughout — `check-signal-probe` is 150 checks and none of them reaches the
refusal, because a synthetic fixture is built at a level that measures.

## Done

- Updated `signal_envelope_stats()` to take `double full_scale` and compare against
  `SIGNAL_ENVELOPE_MIN_RMS * full_scale`.
- Added `float full_scale` to `struct survey_block` across `survey_session.h`,
  `view_survey.c`, `survey_report.c`, and `survey_session_test.c`.
- Updated all call sites in `survey_session.c`, `scripts/ook_report.c`,
  `scripts/signal_report.c`, and `tests/signal_probe_test.c`.
- Added `test_envelope_refusal_is_fraction_of_full_scale()` in `tests/signal_probe_test.c`
  verifying that the refusal fires at the identical fraction of full scale across 8-bit
  and 16-bit containers.
- All checks pass in `check-signal-probe` (186 checks) and `check-survey-session` (158 checks).
