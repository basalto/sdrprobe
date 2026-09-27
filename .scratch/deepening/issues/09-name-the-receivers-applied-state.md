# 09 - Name the receiver's applied state

Status: **needs-info, triaged 2026-09-28** -- three of the seven fields are
absorbed, the remaining four are parked behind a hardware question, and one
of them turns out not to belong here at all. See "Triage, 2026-09-28" below.
Blocked by: `.scratch/device-model/issues/07-a-second-backend.md`
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


## Triage, 2026-09-28

Re-measured rather than re-reasoned. `grep -rln "app-><field>" src/`, against
the table this ticket opened with:

| field | readers then | readers now |
| --- | --- | --- |
| `applied_frequency` | 13 | **0** |
| `applied_sample_rate` | 14 | **0** |
| `applied_ppm` | 11 | **0** |
| `applied_gain_tenths` | 6 | 9 |
| `remove_dc` | 6 | 8 |
| `applied_manual_gain` | 3 | 7 |
| `receiver_mode` | 15 | **27** |

### Three are done, and not by this ticket

`struct receiver_applied` -- frequency, rate, ppm and the tuning generation
ADR-0027 publishes -- came out of ticket 11, not this one. `CLAUDE.md` names
it as one of the two largest reductions in the whole carve-out and says why:
ticket 11 asked what *owns* a field rather than where it should live, and the
answer took the three most widely read of these with it.

That is the useful half of this triage. **The fields with no home found one
when a different question was asked**, which is the lesson `06-*` already
records about `scan_selected_arfcn`, `scan_step_count` and `settings_error`.

### Three are parked, deliberately, behind the second receiver

`applied_gain_tenths`, `applied_manual_gain` and `remove_dc` are the
receiver's applied *configuration* rather than its tuning. They are read more
widely than when this ticket opened, and that is not drift: the Settings
panel's view model and its transaction both read all three
(`web-visualization/17`), which is one more reader by design.

They wait on `device-model/07` for the reason `device_profile.h` already
states about function pointers -- **an adapter with one implementation is a
pass-through**. What a gain *is* differs between an RTL-SDR's 29 discrete
tenths of a decibel and an AD9361's gain-table index, and naming a home for
the current value before a second device has an opinion is inventing a
representation for one device and calling it a contract. `GAIN_UNIT_INDEX`
exists because that distinction is already real on paper; it is not yet real
in this program.

### And one does not belong in this ticket at all

**`receiver_mode` is the odd one out, and it has nearly doubled.** It is not
applied state: it answers *"is this a live receiver or a capture"*, which is
a fact about the **source**, not about how the receiver is set up. That is
why it has no natural home among the other six, and why it keeps spreading --
twenty-seven files, more than `pair_count` or `i_samples` ever reached.

It belongs with `struct acquisition` or `struct device_session`, both of
which already know which they are, and moving it is not blocked by the second
receiver at all. It should be its own ticket. **Not opened here**, because
this one is parked and a ticket parked behind hardware is the wrong place to
keep work that is not.
