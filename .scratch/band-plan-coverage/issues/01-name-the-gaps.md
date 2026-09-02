# 01 — Name what the table cannot

Status: ready-for-agent
Blocked by: (none)

`src/band_plan.c`, and `make check-band-plan` alongside.

## Where to look, in order of what it buys

1. **400-500 MHz**, 16 candidates and the strongest gap in the sweep. In ITU
   Region 1 this is where private and business mobile radio, PMR446, and the
   440 MHz amateur allocation live, and 416.652 MHz specifically deserves an
   answer.
2. **200-300 MHz**, 27 candidates. The table stops after VHF band III; above
   240 MHz it says nothing at all, and 251, 264 and 267 MHz all show above
   -37 dBFS.
3. **300-400 MHz**, 23 candidates. The table has TETRA and little else.
4. **Below 100 MHz**, 13 candidates including 38, 49 and 60 MHz -- low VHF,
   and quite possibly not transmitters at all but the receiver's own
   environment. Worth naming as such if so.

Portugal's arrangement wins where it differs from ITU Region 1, as the header
of `band_plan.c` already says, so ANACOM's table is the source. Cite it in the
comment for each range added, the way the existing entries do.

## Two rules the existing table already follows

- **An allocation, never an identification.** "LTE band 28 downlink" is right;
  "5G" would be wrong even though that is what is there.
- **A gap is better than a guess.** An entry nobody has checked is worse than
  no entry, because the survey prints it as fact. Leave a range unnamed rather
  than filling it from memory.

## Done when

Every candidate in `surveys/2026-09-02-24M-1766M.json` either has an
allocation, or is a frequency deliberately left unnamed for a reason recorded
in the file. Re-running `./scripts/survey_tool.py report` on that same survey
is the measure -- the "(no band plan entry)" row is the number to drive down,
and it needs no receiver to check.
