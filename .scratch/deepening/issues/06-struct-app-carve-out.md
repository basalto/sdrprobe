# 06 - Finish moving view state out of `struct app`

Status: ready-for-agent, 2026-09-09 -- the audit is below and the answer
is **not** "only handoffs". Three follow-up tickets, named at the end.

`app.h` is 1 019 lines. Its own header comment says the carve-out into
`struct scope_view`, `struct gsm_view` and the rest is "an organisation of the
same coupling, not a set of modules": every view still reads the whole record.
The stragglers show where it stalled:

- `gsm_analysis_mode` lives at the top of `struct app` while
  `fm.analysis_mode`, `lte.analysis_mode`, `tetra.analysis_mode` and
  `adsb.analysis_mode` are nested;
- `cal_return_sample_rate` (`app.h:1000`) sits outside `struct calibration`;
- nine `return_*` fields (ticket 01);
- `struct options` has 55 fields, applied in `main` and again in `run_gui`.

## Why this is not a ticket of its own

Nothing here is worth doing directly. Each straggler belongs to one of 01-04:
the `return_*` fields to the lease, `gsm_analysis_mode` and the per-view
decode state to the sessions, `config`-adjacent fields to the installation.
When those land, what is left in `struct app` should be the handoffs between
screens -- tab, decode kind, the sample buffers, the current block -- and the
deletion test should pass for every remaining field: delete it and something
in two files breaks.

## What to do with it

Use it as the measure. After each of 01-04, count `struct app`'s fields and
list which ones two or more files still read. When the list is only handoffs,
close this ticket with the number in `spec.md`; if a field survives that only
one file reads, that is the next straggler and it goes back to whichever
ticket owns it.

## Not in scope

Making views into modules in the full sense (state with code, no `app.h`
include) the way `acquisition.c` is. That is a further step and should get its
own spec if the number above says it is close.


## The audit, 2026-09-09, with 01-04 all resolved

`app.h` is **898 lines** against the 1019 this ticket cites, and `struct app`
holds **85 fields**. Every field's readers were counted across `src/` and
`tests/`; what follows is that count grouped by what the fields are *about*.

Two of the four stragglers named above are gone. The nine `return_*` fields
went to the lease (01) and `cal_return_sample_rate` with them -- both survive
only as comments explaining their own absence. `gsm_analysis_mode` was still
loose, read from five files while `fm`, `lte`, `tetra` and `adsb` each nest
their own; it is `gsm.analysis_mode` now, which is this ticket's one direct
fix. Its comment was false as well as loose -- "Burst Analysis Chart: 0=Corr,
1=Soft Bits, 2=Phase", for a field the view only ever sets to 0, sets to 1 or
toggles, and has not offered three charts through for some time. Three stale
headings went with it -- "ADS-B / Mode S decoder tab",
"GSM SCH decode of the inspected channel" and two about a raw-I/Q recording --
each sitting over no fields at all, left behind by earlier carve-outs.

### What is genuinely a handoff

About **nineteen** fields, and they are the ones this ticket predicted:

- the current block -- `i_samples` (10 readers), `q_samples` (10),
  `pair_count` (11), `magnitudes` (3), `magnitude_sorted` (4),
  `spectrum_average` (7);
- which screen -- `tab` (6), `decode` (3), `view` (5), `plot` (6);
- the program's shared services -- `device` (7), `acq` (5), `source` (3),
  `lease` (3), `options` (5), `config` (5), `installation` (6), `dsp` (1),
  `capture` (1).

A further **eleven** are the per-view containers -- `sv`, `survey`, `gsm`,
`adsb`, `lte`, `fm`, `tetra`, `set`, `help`, `cal`, `bandscan` -- each read by
its own view and `sdrprobe.c` and nothing else. That is the carve-out working:
`bandscan` has exactly one reader outside `app.h`.

### What is not, and this is the finding

The other **fifty-five** fields are four clusters, and **none of them belongs
to 01-04**. This ticket's premise -- "nothing here is worth doing directly,
each straggler belongs to one of 01-04" -- was written before anyone counted.
It is wrong: the survivors belong to *calibration*, *the band scan*, *the
receiver* and *the spectrum display*, and not one of those was a deepening
ticket.

| cluster | fields | reads | read by one file only |
| --- | --- | --- | --- |
| calibration + drift | 16 | 32 | 5 |
| spectrum / magnitude display | 16 | 36 | 3 |
| the receiver's applied state | 7 | 68 | 0 |
| GSM band scan | 8 | 23 | 0 |

**The calibration cluster is the clearest and the most embarrassing.**
`struct calibration cal` exists, and is read by two files -- while sixteen
calibration fields sit *outside* it: `calibration_open`,
`calibration_technology`, `calibration_expected_hz`, `calibration_status`,
`auto_drift_check`, `gsm_cal_valid`, `gsm_cal_ppm`, `gsm_cal_arfcn`,
`lte_cal_valid`, `lte_cal_earfcn`, `lte_cal_ppm`, `cal_lte_band`,
`cal_lte_scanning`, `drift_health`, `drift_notice`, `drift_phase`. Five of
them have a single reader, `overlay_calibration.c`. This is precisely the
shape of the `cal_return_sample_rate` straggler this ticket named -- and
ticket 01 fixed that one field and left the sixteen beside it. The instruction
in `CLAUDE.md` to "reach for `app->cal.*` rather than adding a `calibration_*`
field back to `struct app`" is advice the struct itself does not follow.

**The band scan is the same fault, smaller.** `struct band_scan bandscan` has
one reader; eight scan fields sit outside it, three of them read by four or
five files.

**The receiver's applied state is a different kind of finding: a module with
no name.** Seven fields -- `receiver_mode` (15 readers), `applied_sample_rate`
(14), `applied_frequency` (13), `applied_ppm` (11), `applied_gain_tenths` (6),
`remove_dc` (6), `applied_manual_gain` (3) -- across a union of nineteen
files, and the top three are read more widely than the sample buffers
themselves: fifteen files against `pair_count`'s eleven. They answer one
question: where the receiver is pointed and how it is set up. They are not stragglers from a carve-out that stalled; they never
had a home. `receiver_lease.h` already reasons about two of them and
`device_profile.h` about the rest of the same subject, so the seam is half
drawn already.

**The spectrum display cluster is the weakest case.** Sixteen fields, and
almost all of the reads are `sdrprobe.c` (which computes them) and
`view_scope.c` (which draws them) -- which is what a handoff between two
files looks like. It is listed for completeness and is not worth a ticket
until something else needs it.

### So this ticket cannot close

Its own criterion is "when the list is only handoffs, close this ticket with
the number in `spec.md`". The list is 19 handoffs, 11 containers and 55 fields
in four clusters, so the answer is no. What it *can* do is stop being a
measure and become three tickets, which is the honest outcome of a ticket
whose job was to find out whether there was anything left:

- **07 -- the calibration state into `struct calibration`.** Sixteen fields,
  five of them single-reader, into a struct that already exists. The one with
  the best ratio of size to payoff, and the one `CLAUDE.md` already tells
  people to expect.
- **08 -- the band scan's state into `struct band_scan`.** Eight fields, same
  shape, smaller.
- **09 -- name the receiver's applied state.** Seven fields across nineteen
  files, three of them read more widely than the sample buffers; a new module
  rather than a carve-out, and it wants an ADR-sized argument about who may
  write it, because `retune_receiver()` and the lease both do today. It should
  probably wait for the second receiver: a "what is applied" struct designed
  against one device is the mistake `.scratch/device-model/` exists to avoid.

The number to quote in `spec.md` is the one above: **85 fields, 30 of which
are handoffs or containers.**
