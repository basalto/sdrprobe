# 02 - A decode session per technology, shared by window and headless

Status: resolved. **GSM done 2026-09-08**, as the ticket's own order asks
-- it was already shared, so if extracting it had changed an answer the design
would have been wrong rather than the code. It changed none. **All five done 2026-09-09**: GSM, TETRA, LTE, ADS-B, FM/RDS.
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


## TETRA, the clearest duplication

`src/tetra_session.{c,h}`. `update_tetra()` and `print_tetra()` each ran the
whole chain: coarse offset, channel filter, demodulate, walk the symbols for
the synchronisation word, decode the block, pull MCC/MNC/colour out of it, then
the broadcast channel scrambled with the colour code that block just gave up.

**They were not quite the same, which is the argument for the module rather
than against it.** One pulled its fields with `field(bits, at, count)`; the
other unrolled four bit loops inline. Two spellings of one transcription from
ETSI EN 300 392-2, either of which could have been corrected without the other.
There is one of each offset now.

`struct tetra_view` keeps `analysis_mode`, the constellation points, the slot
profile and the log -- all drawing -- and the decode is `tetra.session`. The
points are derived from the session's symbols each block, because a point on a
circle is a drawing and not a decode.

`rate_unsupported` became an event rather than a silent nothing. The channel
filter refuses a rate that is not a whole multiple of `TETRA_WORK_RATE_HZ`
rather than resampling, and a run at the wrong rate decodes nothing for a
reason that has nothing to do with the signal. Both adapters explained that;
now they explain it from one flag.

### Checked

`check-tetra-session`, 31 checks. Both captures, block by block: colour code 17
with location area 4375, and 32 with 4658. That pair is the point --
`tetra_bnch_decode()` is scrambled with the network's **own** colour code, read
out of the synchronisation block first, so a decoder that hardcoded one would
read one capture and fail the other. **Verified by mutation**: hardcoding
`s->colour = 17` fails three checks by name, including the broadcast one.

It also asserts the funnel is ordered -- broadcasts <= blocks <= bursts -- which
is what makes "which stage stopped" a diagnosis rather than a guess.

### A claim of mine was wrong first, again

The rate-refusal check used 1999999 S/s, expecting it to be refused. It is
**accepted**: `tetra_channel()`'s tolerance is `> 1.0` Hz and 1999999 is
exactly one hertz out. 2.048 MS/s is the rate the check uses now, and it is a
better one for a second reason -- it is what `fm_rds_tsf.bin` is recorded at,
so it is a wrong answer a person could actually give.


## LTE, the largest, and the rule that is the point of it

`src/lte_session.{c,h}`. One block's whole answer: the cell search, the
reference power, the port coherence, the channel shape, the run's statistics,
and the rule that decides when a broadcast message is believed.

**That last one is why LTE is worth a module even though `print_lte()` already
called `update_lte()`.** Thirty-six parity attempts a block -- four scrambling
offsets against three masks, for each of three port hypotheses -- makes a lucky
pass *expected* rather than rare. A message counts only when a second agrees
about what a cell does not change between frames. `check-lte-session` pins that
by mutation: believing the first pass turns 28 messages into 29.

`struct lte_view` keeps the chart window, the EARFCN, the lease token, the band
scan, the analysis mode and its trace, plus `announced_pci` -- which is the
*printer's* business, not the decode's. Everything else is `lte.session`.

`port_hypotheses` existed twice, once in `view_lte.c` and once inside
`run_headless`. It is `lte_session_port_hypotheses` once.

### What is still duplicated, and it is not what this ticket first said

This section claimed `--lte-chain` "keeps its own message counting, and it is a
different rule rather than a second spelling". **That was wrong.** The two are
the same rule, and the duplication is real.

Proven three ways:

- **Exhaustively.** Both rules driven by every sequence of up to eight parity
  passes over three distinct messages -- 9840 sequences -- differ on **none**.
- **By construction.** The session's `pending` always holds the previous pass's
  message (a disagreement replaces it, an agreement leaves it equal), and
  `hits > 0` always holds after the first pass. So `hits >= 2` reduces exactly
  to *"this pass agrees with the previous one"*, which is literally the chain's
  `have_last && lte_mib_same_cell(&last, &mib)`.
- **On air.** A twelve-second run of `--lte-chain` on EARFCN 6200: 162 blocks,
  152 cells, **140 parity, 139 messages**. `messages == parity - 1`, the same
  relation the session gives on `lte_b20_pci28.bin` (28 from 29).

**So `pending_mib_hits` is a boolean wearing a counter's clothes.** It reads
like state accumulating toward a threshold, and it cannot be: because a message
is emitted on *every* pass once it reaches two, it can never mean more than
"did the previous pass agree".

The chain should use the session, and doing so is now provably behaviour-
preserving rather than a hoped-for equivalence.

### A separate finding: "message" means two things in one function

`--lte-chain` prints `lte-chain-summary ... parity N messages N`, where
`messages` is the agreement count above. But the **verdict** it feeds is
`lte_confirm_saw(&tally, cell.pci, primary_read)` -- and `primary_read` is set
on *any* successful decode, a bare parity pass.

Both are internally correct: `lte_confirm.h` documents its own `messages` field
as "blocks in which its broadcast channel decoded", which is exactly
`primary_read`, and `LTE_CONFIRM_MIN_MESSAGES 2` is two such blocks naming the
same identity. The collision is in the **word**, in one function's output: a
reader comparing the summary's `messages` against a `confirmed` verdict is
comparing two different quantities that share a name.

### And two wrong claims in CLAUDE.md, found by measuring

`lte_b20_pci28.bin` "must keep reading cell 32 ... in every block". Both halves
were wrong:

- it reads **cell 28**, which is what its own filename says and what
  `check-pipelines` has always asserted. "32" appeared twice and was never
  true;
- it finds a cell in **29 of 30** blocks. **Block 25 reads a primary sequence
  at 0.80 and no secondary one at all** -- SSS 0.000, the "PSS without SSS"
  case the LTE notes name as their own diagnosis. Pre-existing: calling
  `lte_cell_search()` directly on that block does the same with no session
  involved.

`check-lte-session` pins 29 exactly rather than loosening to "most", because 30
would mean something improved and 28 that something regressed. Whether block 25
*should* decode is a decode question and not a refactoring one; it is now
visible, which it was not.

A diagnostic of mine was wrong on the way to that, and nearly became the
finding: the first version printed the *session's latched* cell rather than the
failing block's attempt, so it reported a strong SSS where there was none.
Asking `lte_cell_search()` directly is what settled it.


## ADS-B and FM/RDS

**ADS-B** was already shared -- `run_headless` called `update_adsb()` -- so
this was an extraction like GSM's. `struct adsb_session` owns the demodulator,
and with it the **even/odd pairing cache**, which is the part that has to
survive between blocks: a global position needs two frames of opposite parity
from the same aircraft. The view keeps the log, the analysis toggle and the
hold, because formatting a message into a row is about looking at frames rather
than reading them.

`check-adsb-session` replays `adsb_cpr_pair.bin` -- six frames, one position --
and then replays it again **resetting the session before every block**. The
frames still come out and the position does not, which is what makes the cache
visibly load-bearing rather than merely present.

**FM/RDS** is the one whose boundary needed deciding rather than following.
`update_fm_flush()` did three things in one pass over the multiplex: decoded
RDS, produced **sound**, and built two chart spectra. Only the first is a
decode -- the sound writes into a raylib `AudioStream` -- so the session is the
RDS chain and the other two stay in the view. The caller does the
discriminator, because it wants the multiplex anyway and doing it twice would
be the same work for the same answer.

### Two of my checks were wrong, in different ways

**The ADS-B one passed a block index as `now`.** Unlike the other four
sessions, ADS-B's timestamp does real work: the pairing cache expires a held
frame after a few seconds, so block numbers stretched the gap between a pair by
about fifteen times and no position ever resolved. It read as a broken cache. A
block is 65.5 ms and the check says so now.

**The FM one named a fault it could not see.** It compared `bit_count` against
`groups * 104 * 4` to catch the window being re-appended every block -- and
passed under both the right code and the mutation, because the bit memory caps
at 32768 either way. Measured instead: appending only what is new gives **2057
bits and 19 groups**, re-appending the whole window gives **4092 and 31**. The
station reads 0x8343 `TSF` under both, so the identity is no help. Those two
numbers are pinned, and the mutation now fails by name.

## Where this leaves the ticket

Five sessions, five checks, `check-pipelines` unchanged throughout. `make
check` is 17493 in 53 suites, from 17332 before this ticket.

`run_headless`'s five branches are adapters now rather than second
implementations -- except `--lte-chain`, which keeps its own message counting
deliberately, because it is a diagnostic reporting every attempt rather than a
decode latching what it believes. That is recorded above as a question worth
deciding rather than a duplication to remove.
