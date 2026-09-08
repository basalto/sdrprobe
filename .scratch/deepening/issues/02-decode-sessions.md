# 02 - A decode session per technology, shared by window and headless

Status: in progress. **GSM done 2026-09-08**, as the ticket's own order asks
-- it was already shared, so if extracting it had changed an answer the design
would have been wrong rather than the code. It changed none. TETRA next, then
LTE.
Blocked by: 01 (done)

Per-block decode orchestration -- feed the block, latch the trace, count the
funnel, decide whether two readings agree -- is written once for the window
and once for `run_headless`:

| technology | window | headless |
| --- | --- | --- |
| TETRA | `view_tetra.c:85-150` | `sdrprobe.c:1849-1890` |
| LTE | `view_lte.c:304,376,456`, MIB agreement `:558-565` | `sdrprobe.c:2251+` (`--lte-chain`, `--decode`) |
| GSM | `update_gsm_sch` (`view_gsm.c:146`) | **shared** -- called from `sdrprobe.c:1557` and `:1962` |
| ADS-B | `view_adsb.c` | `run_headless` `--decode` |
| FM/RDS | `view_fm.c:273` chunk accumulation | `run_headless` `--decode` |

GSM is the exception and the shape to copy. Everywhere else, the "second MIB
that agrees", the funnel, and the latched trace have to be kept in step by
hand, and the only check that reaches either copy is `check-pipelines`
through the built binary -- ADR-0012's layer 2 for something that is
arithmetic and belongs in layer 1.

`run_headless` is 760 lines (`sdrprobe.c:1995-2755`) with five branches
(`+49 lte_scan`, `+120 lte_chain`, `+543 calibrate`, `+683 survey_report`,
`+706 decode`); most of it is orchestration that the views also have.

## The deepened module

One session module per technology, `<tech>_session.{c,h}`, with a block-in,
events-out interface: feed it centred I/Q and a timestamp, read back what
changed -- a decoded message, a funnel delta, a new latched trace, a stat that
moved. It owns the state the view keeps today (`pending_mib`, `pending_mib_hits`,
`lte_stats`, `adsb.analysis` trace, the TETRA symbol history, the FM bit
accumulator). No raylib, no receiver, `-lm` only.

The view draws the session; `run_headless` prints it. Both are adapters.

Per ADR-0023 the sessions need not share an interface with each other -- the
LTE one has a MIB agreement rule and the ADS-B one has a CPR pairing cache --
only the dependency rule.

## What moves

- The `update_*` / per-block halves of `view_gsm.c`, `view_lte.c`,
  `view_adsb.c`, `view_fm.c`, `view_tetra.c` into their sessions.
- The matching blocks of `run_headless`, which then formats session events.
- `struct lte_view`, `struct adsb_view` etc. shrink to drawing state; the
  session state moves into the session.

## Checks

One `check-<tech>-session` per technology, replaying the capture in
`testfiles/` block by block and asserting the event sequence -- the same
assertions `check-pipelines` makes today, reachable in milliseconds and
without a process launch. `check-pipelines` stays as the proof they are wired
in.

The claims to pin, from CLAUDE.md: `lte_b20_pci28.bin` reads cell 28 with 2
ports in every block; `tetra_cc17.bin` and `tetra_cc32.bin` read their own
colour codes; `adsb_cpr_pair.bin` resolves one position from 6 frames twice.

## Order

Do GSM first as the proof it changes nothing (it is already shared), then
TETRA (smallest duplication, clearest pair), then LTE (largest).

## Not in scope

The Scope tab and the survey (ticket 04). Calibration is a session already in
all but name (`overlay_calibration.c` and `--calibrate` share the gate) and can
follow later.


## GSM, the proof it changes nothing

`src/gsm_session.{c,h}`. `update_gsm_sch()` was already called from both the
window and `run_headless`, which made GSM the shape to copy -- but it was not a
session: it took `struct app` and read `i_samples`, `pair_count`,
`applied_sample_rate` and six fields of `gsm_view` out of it, so nothing but
the program could drive it.

Now it takes samples and gives back events. `gsm_session_feed(session, i, q,
pairs, rate, offset_hz, now, &event)` returns whether anything was read and
fills a `gsm_session_event` -- `sch_decoded`, `broadcast_read`, and the
`gsm_si` behind the second. `struct gsm_cell` moved out of `app.h` with it,
along with `gsm_read_broadcast()`, which is now a static inside the session
because nothing else ever called it.

`struct gsm_view` keeps what a view actually needs: the chart window, the
selected channel, the lease token, and the two constellation toggles. The
decode is `gsm.session`.

### One representation for the front-end options

`opt_filter`, `opt_finecfo` and `opt_trellis` were three ints in the view. The
decoder has always taken a `GSM_OPT_*` **mask**, so the view rebuilt one every
block and `--gsm-features` unpacked a mask into the three ints to feed it. Now
there is one mask, `gsm_session_option()` / `_set_option()` / `_toggle_option()`
read and write it, and the command line's mask goes straight through.

### Checked

`check-gsm-session`, 49 checks, replaying all three captures block by block --
no window, no receiver, no process launch. BSIC 59, 38 and 56, the three BCCs
that are the point of having three captures; MCC 268 MNC 03 LAC 4010 Cell
Identity 5131; frame numbers that never go backwards. It also does what
`check-pipelines` needs two process launches for: the same capture through two
sessions differing only in their option mask, asserting the refinements decode
more.

`make check` is 17381 in 49 suites, and `check-pipelines` is **unchanged** --
which is the result this step was for.

### One claim of mine was wrong first

The check asserted all 31 blocks decode an SCH. Twenty-eight do, with the
options at zero, which is what `memset` leaves. The refinements are what makes
it 31, and the check states which mask it is replaying under rather than
leaving it to a default -- and asserts both, since the comparison is more
informative than either number alone.
