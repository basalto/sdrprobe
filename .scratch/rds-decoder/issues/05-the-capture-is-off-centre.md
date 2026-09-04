# 05 — The TSF capture is tuned 100 kHz off the station

Status: resolved
Blocked by: (none)

`testfiles/fm_rds_tsf.bin` was recorded at 89.6 MHz. The band scan later put
TSF at 89.5, and the scan's raster snapping is checked and correct, so the
capture is 100 kHz off centre.

It decodes perfectly -- an FM carrier is 200 kHz wide, so 100 kHz off still
lands inside it -- and every check that uses the file passes. The sidecar is
accurate: it records what was tuned, and what was tuned was 89.6.

So this is not a fault, it is a slightly worse capture than it could be: the
signal sits on the shoulder of the tuner's response rather than in the middle
of it, which costs signal-to-noise the checks are currently absorbing.

## The work

Only worth doing if that file is regenerated for another reason -- re-record
at 89.5, confirm the block rate is at least what it is now (78 of 91 blocks in
two seconds), and update the sidecar and `fm_rds_tsf.json`'s notes.

Not worth doing on its own. Replacing a capture that works costs a review of
every check that reads it.

## Decision

**Re-record it, tuned to 89.5.**

The capture should mean what its filename and its sidecar say. As it stands
the 100 kHz offset is load-bearing by accident: it is exercising the
decoder's tolerance of an offset, and doing so silently, so a future change
that lost that tolerance would fail here for a reason nobody had written down.

Needs the receiver and needs TSF still on air at 89.5 here -- check before
assuming, since two of the three GSM captures went off the air with the
operator's refarming and cannot be re-recorded.

Keep the invariants `check-pipelines` already asserts: identification 0x8343,
the name `TSF`, and the programme type. The programme type is not decoration
-- it lives in a different block of every group, and the name alone would pass
with the differential sense backwards.

If TSF has gone, say so and close this rather than substituting another
station: the checks name this one.

## Comments

Resolved. Re-recorded at 89.5, three seconds, and TSF was still there.

Two things were checked rather than assumed, and both changed the answer.

**On-centre had to be shown to be no worse.** An FM carrier at 0 Hz sits
exactly where the receiver's own DC spike is, which is a real reason to tune
off a station deliberately -- so the old offset might have been a technique
rather than a slip. Two three-second captures of TSF minutes apart settled it:
89.5 read 19 groups agreeing, 89.6 read 18. No penalty for being on the
station, so the offset was a slip after all.

**Two seconds was a coin flip, and the old capture had been winning it.** The
first re-recording, at two seconds, decoded the identification and *not* the
name. A name is four two-character segments seen whole twice and agreeing --
one pass through four segments can be four segments of two different names --
so whether two seconds is enough depends on where the segment cycle happens to
fall. One second of the final capture names nothing; two seconds of it does;
a separate two-second recording minutes earlier did not. `check-pipelines` has
been asserting the name against a capture that had got lucky. Three seconds is
margin instead, at the cost of four megabytes.

`check-pipelines` now also asserts the tuning: `station 0x8343 at 89.500 MHz`.
The old capture was tuned 100 kHz off for three days with nothing able to
notice, which is the whole complaint this ticket was filed about, and the
assertion is what stops it recurring silently.
