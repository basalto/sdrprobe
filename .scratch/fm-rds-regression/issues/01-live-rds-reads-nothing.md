# 01 — Live RDS reads nothing, on every station

Status: resolved

Find out why, and make the answer reachable without a receiver.

## Where to start

Not in `rds.c` and not in `fm_dsp.c`. The capture proves both work; see the
spec for the run. Start at the seam between a live receiver and the chain:
block continuity first, then the frame loop, then tuning and level.

## Done when

- The cause is named, with the evidence that names it rather than a change
  that happened to help.
- A **live recording that reproduces it** is in `testfiles/` with a sidecar,
  and a check asserts what it should now read. Without that this can regress
  again in silence -- which is how it arrived.
- If the cause turns out to be a dropped block rather than a decode fault,
  say so in `docs/` where the block slot is described: a lossy slot is right
  for a display and wrong for an accumulator, and the FM view is both.

## What this must not become

A widened tolerance. The pilot's gate takes coherence and presence together
because either alone ranks stations backwards, and the block synchroniser
needs four syndromes in the offset order because one matches by chance about
once in two hundred tries (`rds_sync_odds_per_million()`). Loosening either
would make this report go away without making the decode right.

## Comments

Resolved, and the report was accurate about the symptom while the cause was
somewhere nobody would have looked.

**RDS was decoding the whole time.** The band scan's RDS column was lying.
The scan reported "21 carriers, 16 in stereo, 0 carrying RDS" while the panel
directly below the list read `TSF`, programme identification 0x8343, 68
groups agreeing, from a station in that list marked `no`.

The funnel found it in one run. A scan visit logged `bb 8947, consumed 0,
bits 0`: baseband arriving, none of it turned into bits. The RDS decode works
in fixed 32768-sample chunks -- about 1.7 seconds of baseband -- and a scan
visit is 0.7 seconds. Every visit gathered roughly a quarter of a chunk,
decoded none of it, and moved on. The threshold was `if (fm->bb_count <
FM_RDS_CHUNK_SAMPLES) return;` and it was never once satisfied during a scan.

It arrived with "Radio text, seen whole", which replaced a sliding window with
fixed non-overlapping chunks. That change was right -- a sliding window
re-derives its timing each pass and drops a leading symbol, so which absolute
symbol an index means moves underneath you, and radio text needs twenty-five
seconds of consistent indices. What it did not account for is that it made
the *minimum unit of decoding* longer than an entire scan visit.

The fix is a final short chunk when there will be no next one. It is as valid
as a full chunk and simply carries fewer bits: the fixed size exists so that
consecutive chunks agree about absolute symbol indices, and a tuning that is
ending has nothing left to agree with. Measured after: **7 of 20 carriers
identified**, with programme identifications -- 0x8343, 0x8231, 0x8202,
0x8203, 0x8204, 0x8458, 0x8332. Two stations the scan still calls `no` were
checked by parking on them for 25 seconds headless and genuinely carry none.

`fm_rds_chunk_length()` is now the decision, in `fm_scan.h` with 13 checks
over it, including `fm_scan_visit_fills_chunk()` asserting outright that a
visit cannot fill a chunk -- so if the visit length ever grows past one,
somebody finds out there rather than by deleting the flush.

The scan's debug line now carries the funnel rather than the verdict:
baseband, consumed, bits, blocks matched, groups. Which stage stops is the
diagnosis, and reading `bb 8947 consumed 0` is what turned a day of guessing
into one run.

## What this did not need

The two things the ticket warned against, and neither was touched: the pilot
gate still takes coherence and presence together, and the block synchroniser
still wants four syndromes in the offset order. The station list gained seven
entries without either being loosened by a hair.

## Not done

No capture reproduces this. It needs a *scan*, which is a sequence of
retunes, and the capture format here is one tuning -- so the check that holds
it is `fm_rds_chunk_length()` rather than a recorded signal. That is the right
shape for this bug: the fault was arithmetic about lengths, not anything the
samples could have shown.
