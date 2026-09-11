# 06 - Finish moving view state out of `struct app`

Status: **resolved 2026-09-11. 37 fields**, down from 85 when this ticket was
opened. What is left is containers, handoffs, ticket 09's three, and six that
belong to `sdrprobe.c` alone -- which is a new straggler this audit found and
is recorded below rather than left implicit.

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

## After 07 and 08, 2026-09-09

**62 fields.** Twenty-three gone in two tickets, and both of them found a
field whose *owner* was wrong rather than merely its address -- which is the
question this audit could not ask, because counting readers says where a
field is used and not what it is about:

- 07: `calibration_status` was also the receiver's error line, written by
  `retune_receiver()` from every screen and prefixed "Calibration". Now
  `app->receiver_error`.
- 08: `scan_selected_arfcn` was the GSM view's inspected channel, set by
  `--arfcn` with no scan involved. Now `gsm.selected_arfcn`. And
  `scan_step_count` was a copy of `bandscan.plan.step_count`, so it was
  deleted rather than moved.

**60 fields** after 08's follow-up took `settings_open` and `settings_error`
into `struct settings_panel`, where the same fault turned up a *third* time:
the acquisition lifecycle was writing its failures into `settings_error`, and
because `retune_receiver()` stops and restarts acquisition, that left ticket
07's own `receiver_error` stale on the path it was created for.

**Three tickets, three fields whose owner was wrong**, and the audit could not
have found any of them: counting readers says where a field is used, never
what it is about. The question that did find them is one line -- *is this
field doing the job its name claims?* -- and it is worth asking of every field
before moving it, not after.

What is left of the four clusters is one: **the receiver's applied state**,
ticket 09, which is `needs-triage` and should wait for the second receiver.
The spectrum display cluster stands as it was -- `sdrprobe.c` computing for
`view_scope.c` is a handoff. All four overlay flags are nested.

## Closed, 2026-09-11: 37 fields

`struct app` holds **37 fields**, against 85 when this ticket was opened, 62
after 07 and 08, and 60 after 08's follow-up. The twenty-three since then came
from two tickets that were not carve-outs at all:

- **`11-acquired-signal-frame`** took about twenty loose arrays, counters and
  ready flags into `struct signal_frame` -- centred I/Q, magnitudes, their
  statistics, the DC-filtered copy, the transform and its peak hold. They were
  never a cluster anybody had named; `process_block()` left them on
  `struct app` for every view, overlay, session and headless path to read.
- **`10-receiver-runtime`** folded the three applied fields into
  `struct receiver_applied`, because a rollback over three separately owned
  fields is three chances to restore two of them.

Neither was opened to shrink this record, which is worth noticing: the two
largest reductions came from asking *what owns this* rather than *where should
this live*.

### What the 37 are

| group | n | verdict |
| --- | --- | --- |
| per-view and subsystem containers | 20 | correct -- `sv`, `survey`, `gsm`, `adsb`, `tetra`, `lte`, `fm`, `set`, `help`, `cal`, `bandscan`, `acq`, `options`, `config`, `installation`, `frame`, `applied`, `device`, `lease`, `source` |
| handoffs between screens | 8 | correct -- `tab` (6 files), `plot` (10), `view` (5), `remove_dc` (5), `receiver_error` (5), `decode` (3), `source_label` (2), `waterfall_lower_dbfs` (2) |
| the receiver's applied state | 3 | **ticket 09** -- `receiver_mode` (14 files), `applied_gain_tenths` (5), `applied_manual_gain` (3) |
| `sdrprobe.c`'s own process lifecycle | 6 | **new, see below** |

The deletion test passes for the first two groups: delete any of them and two
or more files break. The ticket's closing condition was "when the list is only
handoffs", and it is, apart from two groups that each have an owner.

### Ticket 09's three are correctly parked

`receiver_mode` is read in **fourteen files**, more widely than the sample
buffers ever were, and ticket 09's own triage says the interesting question is
whether it belongs with the other two at all -- it is which *kind* of source is
open, which `device_backend.h` and `device_profile.h` already know, so it may
be deleted rather than moved. That, and where gain belongs, are exactly what
one device cannot answer. Parked for the second receiver, correctly.

### The new straggler: six fields only `main` reads

`window_ready`, `signals_ready`, `capture`, `tuner_label`, `old_sigint` and
`old_sigterm` are read by **`src/sdrprobe.c` and nothing else**. By this
ticket's own rule -- "a field that survives that only one file reads is the
next straggler" -- they are stragglers.

But the rule needs one qualification this audit is the first to need, because
until now every single-reader field was read by a file that did **not** own it.
These are read by the file that *does*: `sdrprobe.c` owns `main`, the frame
loop, the worker thread and the signal mask, and these six are that file's
process lifecycle. A field read once by its owner is untidy; a field read once
by somebody else is misplaced. Only the second was ever the fault this ticket
was chasing.

So the finding is smaller than it looks and is recorded rather than actioned:
they belong in a `struct` beside the frame loop or as file statics, which is
tidying rather than decoupling, and neither would make a view any less coupled
to `app.h`. **Whoever next opens `sdrprobe.c` for another reason should take
them**; opening a ticket to move six fields inside one file would be
speculative generality of the kind this repository's deletion test exists to
refuse.

### What this does not claim

`app.h`'s own header comment still stands and is the thing to read next: every
view still includes it and reads one big record, so this is an organisation of
the same coupling, not a set of modules. **37 fields is a smaller record, not a
decoupled one.** The change that would make views into modules -- state with
code, no `app.h` include, the way `acquisition.c` is -- is this ticket's own
"not in scope" and wants its own spec.
