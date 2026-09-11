# 02 - A channel raster in the band plan, for the half that has no grid

Status: ready-for-agent -- **decided 2026-09-11: a raster only where no decoder
exists.** See the comments.
Opened 2026-09-11, from `.scratch/device-model/issues/11-*` landing.

`reading_origin_for()` takes a nominal frequency the caller proposes, and
`survey_suspect_origin()` proposes exactly two: the coarse comb multiple and
the fine one. Both are facts about the **receiver**. Nothing proposes a fact
about a **service**, so the `EXTERNAL` half of the verdict is very nearly
unreachable in the shipping program and `UNEXPLAINED` is weaker than it should
be.

`reading_external_channel_hz()` and `reading_raster_is_resolvable()` exist and
have no caller outside `check-reading-origin`. That is the condition this
repository has twice deleted functions for, so this ticket either fills them
or they go.

## What the band plan would gain

Two fields on `struct band_plan_entry` -- `raster_hz` and `raster_base_hz`,
both 0 for "no raster known" -- and the lookup already in place. It fits what
the table is: ADR-0015 says it records what a band is *for*, and the channel
grid a service uses is the same kind of transcribed, static fact as the band
edges already are.

## What it would make possible

**`UNEXPLAINED` becomes a real finding.** Today it means "a bare carrier on no
modelled comb", which is one grid. Ticket 11's property 3 asks for "no
modelled comb **and** no channel raster", and the second half is unimplemented
-- said plainly in that ticket rather than quietly approximated.

**The airband's one external carrier becomes expressible.** 132.062744 is
explicable only on the 8.33 kHz raster: the channel is 132.066667, which
predicts 132.062462 against 132.062744 observed -- 282 Hz -- while the nearest
25 kHz channels are 12.3 and 12.7 kHz away. `.scratch/am-airband/issues/02-*`
planned to declare 8.33 kHz out of scope and would have refused the only
traffic this site has ever measured.

## The trap, which is already in the code and must not be undone

**The channel an external source would be on is found from the *inverted*
reading, not from the reading.** For any raster finer than twice the
separation those are different channels, and every narrow-channel service is
in that regime: at 132 MHz and 31.84 ppm the separation is 4.2 kHz against an
8.33 kHz step. `reading_external_channel_hz()` is the arithmetic and
`check-reading-origin` pins it; a caller reaching for
`reading_nearest_channel_hz()` instead will get a plausible wrong channel.

## Why this is triage and not ready, and it is the scope

**Eighty allocations, and most of them must stay 0.** The table's own rule is
"an allocation, never an identification, and a gap in preference to a guess",
and a raster invented for an allocation nobody has checked is exactly the
guess it refuses. The defensible starting set is small and each entry wants
its source named the way the band edges name the QNAF:

| allocation | raster | base | why it is defensible |
| --- | --- | --- | --- |
| VHF airband | 25/3 kHz | 118.000 MHz | 8.33 kHz channelling; every 25 kHz channel is on it, since 25000 is exactly three steps |
| FM broadcast | 100 kHz | 87.5 MHz | `src/fm_scan.h` already walks this raster and `check-fm-scan` asserts its coverage |
| GSM 900 | 200 kHz | from the ARFCN map | `gsm_dsp.c` already computes it; the raster would be a second statement of it |
| TETRA | 25 kHz | needs sourcing | the two captures are 25 kHz apart and `tetra_cc32.bin`'s note says so |

Three of those four are **already expressed somewhere else in the program**,
which raises the question this ticket has to answer before anything is typed:
is a raster column a new fact, or a fourth copy of `fm_scan.h`'s and
`gsm_dsp.c`'s? A table that disagrees with `gsm_arfcn_hz()` about where ARFCN
1 is would be worse than no table. The honest options are a raster column that
*derives* from those where they exist, or a raster column only for
allocations with no decoder behind them -- which is where it is needed anyway,
since an allocation with a decoder can be asked directly.

**And a raster is regional.** 8.33 kHz channelling is European; the 25 kHz
grid is not universal either. The band plan is already "Portugal's, which is
to say ITU Region 1 as ANACOM applies it", so this inherits that scope and
should say so rather than implying the grid travels.

## What must be checkable

- Every non-zero raster divides its allocation's width a whole number of times
  from its base, or the base is wrong -- the failure that puts every channel
  half a step out and reads as unexplained everywhere.
- `reading_raster_is_resolvable()` refuses any raster finer than twice a
  confirmation pass's bin, so a grid nobody can resolve names no channels.
- The airband survivor lands on 132.066667 and on no 25 kHz channel, which is
  `check-reading-origin`'s case today and would become the band plan's.
- No raster contradicts `gsm_arfcn_hz()` or `fm_scan.h` where both speak.

## Comments

**Decided 2026-09-11.** A raster **only for allocations with no decoder behind
them**, which is where it is needed anyway: an allocation with a decoder can be
asked directly, and `gsm_arfcn_hz()` and `fm_scan.h` already own those grids.

This answers the triage question in the negative, and the negative is the
useful half. A raster column covering GSM 900 and band II would be a **fourth
statement** of facts two modules already hold, and a table that disagrees with
`gsm_arfcn_hz()` about where ARFCN 1 is would be worse than no table. The
deletion test settles it: delete a raster for an allocation with a decoder and
nothing breaks, because the decoder's own map is authoritative.

So the starting set is small and each entry names its source the way the band
edges name the QNAF. The airband's 25/3 kHz from 118.000 MHz is the one with a
measurement behind it -- it is the only grid that explains this site's one
external carrier, at 282 Hz.

**`BAND_PLAN_DECODER_COUNT` is the gate**, which is convenient: an entry whose
`decoder` is not `BAND_PLAN_NONE` may not carry a raster, and that is a
property a check can assert over the whole table rather than a rule somebody
has to remember.

**Scope inherits the table's.** The band plan is "Portugal's, which is to say
ITU Region 1 as ANACOM applies it", and 8.33 kHz channelling is European. The
raster column says so rather than implying the grid travels.
