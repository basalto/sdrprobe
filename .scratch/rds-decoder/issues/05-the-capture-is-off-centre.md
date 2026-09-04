# 05 — The TSF capture is tuned 100 kHz off the station

Status: needs-triage
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
