# 09 - Name the receiver's applied state

Status: needs-triage
Opened by the audit in `06-struct-app-carve-out.md`, 2026-09-09.

Unlike 07 and 08 this is not a carve-out that stalled. These seven fields
never had a home:

| field | readers |
| --- | --- |
| `receiver_mode` | 15 |
| `applied_sample_rate` | 14 |
| `applied_frequency` | 13 |
| `applied_ppm` | 11 |
| `applied_gain_tenths` | 6 |
| `remove_dc` | 6 |
| `applied_manual_gain` | 3 |

**68 field-file pairs across a union of nineteen files**, and the top three
are read more widely than the sample buffers themselves: `receiver_mode` is
in fifteen files against `pair_count`'s eleven and `i_samples`' ten. They
answer one question -- where the receiver is pointed and how it is set up.

Half the seam is drawn already. `receiver_lease.h` reasons about the frequency
and the rate (it is what a token *is*), and `device_profile.h` describes the
gain model, the correction and the container. What is missing is the *current
value* of what those two describe, which is why it is scattered: neither of
them is the wrong place, and there is no right one.

## Why this is triage and not ready

Three questions have to be answered before it is worth touching, and only the
first is a matter of taste.

**Who may write it.** Today `retune_receiver()` and
`retune_receiver_at_rate()` write the frequency and the rate,
`receiver_lease.h` reads them to know what to put back, and the settings panel
writes the gain and the correction. A struct with one writer would be an
improvement; a struct with the same four writers and a new name would be
speculative generality, which this repository's deletion test rejects.

**Whether `receiver_mode` belongs with them at all.** It is not a setting --
it is which *kind* of source is open, and `device_backend.h` and
`device_profile.h` both already know that. Fifteen readers of a boolean that
`device_session` could answer is worth its own look, and it may be that this
field simply goes rather than moves.

**Whether it survives the second backend.** UHD's `recv()` carries sample
timestamps and overflow flags that librtlsdr cannot provide
(`.scratch/device-model/issues/07-*`), and a "what is applied" struct written
now would be designed against one device again -- which is the mistake the
whole device-model spec exists to avoid. **This ticket should probably wait
for that board**, and saying so is the point of writing it down now.

## Not in scope

- ADR-0002. The slot stays a single overwriteable slot.
- Making the views into modules that do not include `app.h`. `06`'s own
  "not in scope" covers that and it is a further step.
