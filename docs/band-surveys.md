# Band surveys

One JSON per sweep, kept so they can be compared. The data lives in
`surveys/`, which is **gitignored**: a sweep measures one location with one
antenna, the same kind of thing as `captures/`, and it is nobody else's
baseline. This file is the format; the sweeps are yours.

A single survey says what is transmitting; A single survey says what is
transmitting; a series says what changed, which is the more useful question and
the only one that needs the files to accumulate.

## Recording one

```sh
./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
    | ./scripts/survey_tool.py ingest --note "where the antenna was, and why"
```

The note is the part a later reader cannot recover. Levels are only comparable
between sweeps taken the same way, so record the antenna, the gain if it was
set, and anything unusual about the location -- a survey with no note is a
number nobody can use as a baseline.

24-1766 MHz is what an R820T reaches, and at 0.12 s a step it takes about four
minutes.

## Reading them

```sh
./scripts/survey_tool.py report surveys/<one>.json
./scripts/survey_tool.py diff  surveys/<older>.json surveys/<newer>.json
```

`report` groups by the band plan's own name for each frequency, which is what
turns a list of 247 candidates into a finding. `diff` compares only where the
two sweeps overlap, and says what appeared, what went, and what moved by 8 dB
or more.

**`diff` refuses two sweeps from different sites.** They are not a before and
an after; comparing them reports the move as though the band had changed, which
is the one way this archive can mislead rather than merely disappoint. Pass
`--force` if you have a reason. It also warns when two sweeps share a site
label but their fingerprints do not -- one of them was probably taken somewhere
else -- and when the antenna differs.

The `rf-environment` skill in `.claude/skills/` is the analysis layer over
these: what is known, what is new, and what is worth measuring next.

## What is in a file

`schema`, `recorded_at`, `range_hz`, `sweep` (steps, bins, dwell, blocks),
`receiver` (tuner, antenna, gain_db), `site` (label, fingerprint), `totals`,
and `candidates` -- each with `hz`, `dbfs`, `prominence_db`, `centre_hz`,
`width_hz`, `flags` and `allocation`.

Two conventions worth knowing before comparing anything:

- **Flagged candidates are kept, never dropped.** A `flags` entry means the
  survey thinks the candidate resembles the receiver rather than the band -- a
  reference comb, a DC offset at a step centre. The survey does not remove
  them and neither does this, because removing a peak would hide a real
  transmitter that happens to sit on a harmonic (ADR-0015). `report` and `diff`
  set them aside and count them; they stay in the file.
- **`allocation` is a lookup, not an identification.** It says which
  allocation the frequency falls in, and nothing about what is actually
  transmitting there. Band 28 in the 2026-09-02 sweep is labelled "LTE band 28
  downlink" and is carrying 5G NR.

## Compare like with like

`diff` warns when two sweeps used a different dwell or gain, because it cannot
tell sensitivity from change. A longer dwell finds weaker peaks, so diffing a
0.12 s sweep against a 0.25 s one over the same band reports most of the
difference between the *sweeps* as though it were a difference in the air. Use
the same parameters for anything meant as a baseline.

## Absence is weak evidence

A 0.12 s dwell catches a bursty transmitter about as often as it misses it, so
one sweep finding nothing at a frequency means little. Two sweeps agreeing
means considerably more. This is the same rule the LTE band scan had to learn
the hard way -- see `LTE_SCAN_MIN_LOOKS` in `src/lte_scan.h`.
