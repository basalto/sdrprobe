# 01 — Name what the table cannot

Status: resolved
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

The "(no band plan entry)" row of `./scripts/survey_tool.py report` is the
measure: every candidate in a full-range sweep either has an allocation, or is
a frequency deliberately left unnamed for a reason recorded in `band_plan.c`.

Survey data is local and gitignored, so the sweep this began from is not in the
repository -- the numbers above are the record of it, and a fresh sweep is four
minutes:

    ./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
        | ./scripts/survey_tool.py ingest

A different location will find different gaps. The table is Portugal's, so
name what is allocated rather than what one sweep happened to hear.

## Answer

**Every clean candidate in a full-range sweep now has an allocation.** 83 had
none before; a fresh sweep of 24-1766 MHz gives 189 clean candidates and zero
without one. The 35 the survey flags as resembling the receiver are not
expected to have allocations and do not.

Source: ANACOM's Quadro Nacional de Atribuicao de Frequencias, 2010/2011
edition of 20 June 2012, whose table gives allocated services and principal
national applications side by side. Cited per range in `band_plan.c`, with the
band edges spelled as the QNAF spells them.

What was added, and what it answered:

| range | allocation | candidates it named |
| --- | --- | --- |
| 30-47 MHz | Land mobile, private networks | part of the low-VHF cluster |
| 47-68 MHz | VHF band I television, analogue off since 2012 | 5 |
| 68-74.8, 75.2-76 MHz | Land mobile, 80 MHz plan | the rest of low VHF |
| 74.8-75.2 MHz | Aeronautical radionavigation, ILS markers | the last one left |
| 148-156, 162.05-174 MHz | Land mobile, 160 MHz plan | 5 |
| 240-328.6, 335.4-380 MHz | Fixed and mobile, conditioned band | **40** |
| 328.6-335.4 MHz | Aeronautical radionavigation, ILS glide path | 2 |
| 403-406 MHz | Meteorological aids, radiosondes | 1 |
| 406-406.1 MHz | Emergency beacons, COSPAS-SARSAT | - |
| 406.1-430 MHz | Fixed links, point to point | 6 |
| 440-446, 446.2-470 MHz | Land mobile | several |
| 694-703, 733-758 MHz | 700 MHz guard band and centre gap | 4 |
| 1610-1660.5 MHz | Mobile-satellite uplink | 3 |

The 240-380 MHz block was the whole of the problem: 40 of the 83 sat there,
and the QNAF gives it as FIXO and MOVEL in every row, each marked *faixa
condicionada*.

**One thing deliberately not claimed.** That note marks a great deal of
230-400 MHz and its legend is not in the edition this was read from, so the
entries say what the allocation *is* -- fixed and mobile -- and say nothing
about what the condition is. Naming it "military" would have been the obvious
guess and is exactly what the ticket forbids.

Two checks were re-blessed rather than worked around. `check-band-plan`
asserted that 700 MHz had no entry, which was true when a gap there was
deliberate; the guard band is a real thing the APT700 arrangement leaves
empty, and a survey saying "nothing is allocated here" beats one saying "the
table does not know".

## Comments

**2026-09-03.** Worth knowing for next time: an allocation is written into a
saved survey at ingest, not looked up when it is reported. Extending the table
does not improve a sweep already on disk -- it has to be swept again, which is
four minutes.
