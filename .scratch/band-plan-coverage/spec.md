# The band plan is silent where the strongest signals are

The 2026-09-02 full-range sweep found 247 candidates. **88 of them fall in
frequencies the band plan has no entry for** -- a third of everything the
receiver heard, including 416.652 MHz at -23.9 dBFS, the loudest thing in the
sweep that is not broadcast radio.

Where they sit:

| range | count |
| --- | --- |
| 0-100 MHz | 13 |
| 100-200 MHz | 6 |
| 200-300 MHz | 27 |
| 300-400 MHz | 23 |
| 400-500 MHz | 16 |
| above 500 MHz | 3 |

The two clusters are the story. 200-300 MHz and 300-500 MHz between them hold
66 candidates the table cannot name, and 416.652 MHz sits in the second with
33.5 dB of prominence -- not a spur, not a harmonic, and not flagged as the
receiver's own.

## Why this matters more than tidiness

ADR-0015 settles what the band plan is for: it is a **lookup**, and printing
the allocation a frequency falls in is the only claim the survey is allowed to
make about identity. A gap in the table is therefore not a cosmetic problem --
it is the survey losing the one thing it can honestly say. A reader looking at
416.652 MHz gets a strong, prominent, unexplained peak and no way to tell
whether it is a licensed service, an unlicensed one, or something in the room.

It also makes the surveys harder to compare over time, which is the whole
purpose of keeping them (`surveys/README.md`): "88 unallocated" is not a
baseline anyone can diff against usefully.

## What this is not

Not an identification. Filling in the table says what a frequency is *for*, and
nothing about what is transmitting -- the 2026-09-02 sweep labels band 28
correctly as an LTE downlink allocation and it is carrying 5G NR. Keep that
distinction in the wording of every entry added, as `band_plan.c` already does.
