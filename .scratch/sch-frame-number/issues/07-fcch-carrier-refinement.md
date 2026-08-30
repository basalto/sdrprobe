# 07 — The SCH carrier refinement locked onto the wrong tone

Status: resolved

## Symptom

Half the blocks of `testfiles/gsm_arfcn_73.bin` produced no SCH decode at all —
16 of 30 — while the strong `gsm_arfcn_69.bin` decoded every block. Easy to read
as a sensitivity limit on a weaker signal. It was not.

## Diagnosis

Instrumenting each block's carrier refinement and best training-sequence match:

```
block  fcch_conf  refined_Hz     train  outcome
    0      0.986      305443     0.730  no burst above threshold
    2      0.998      370318     1.000  decoded
    7      0.998      370161     0.968  decoded
    9      0.986      306627     0.714  no burst above threshold
```

Two things stand out. The training match is **bimodal** — about 1.0 or about
0.73, never between — and 0.73 is just the noise floor of a correlation
maximised over ~140k candidate positions, i.e. those blocks contain no findable
burst. And the blocks that fail refine the carrier to ~305 kHz where the ones
that succeed refine to ~370 kHz: **65 kHz out**. A carrier error that large
breaks the differential demod, so the training sequence cannot be found.

`gsm_fcch_detect`'s `search_window_hz` is a **half-width**, and
`gsm_sch_decode` passed 100 kHz. So the detector would accept the most
tone-like thing anywhere within ±100 kHz of the expected FCCH, report high
coherence for it (0.986, comfortably over the 0.9 threshold), and hand back a
refinement nothing downstream sanity-checked.

## Fix

`GSM_SCH_CARRIER_SEARCH_HZ = 50000.0` — about 53 ppm at GSM 900, generous for
an RTL-SDR and more so once a PPM correction is applied. Narrowed only in the
SCH decode path; channel calibration keeps its wider search, because finding a
large unknown offset is exactly its job.

```
                     before          after
gsm_arfcn_69.bin     31/31 blocks    31/31
gsm_arfcn_73.bin     16/30 blocks    30/30
```

All 14 recovered blocks are genuine: BSIC 56 on all 30, T1 constant at 1576,
and every decoded frame number still advances by exactly the burst's own
spacing (0 mismatches).

## Guard

The real-capture test's decode floors went from 5 and 10 to 25 and 25. Slack
floors would have hidden this. Rebuilding the test with the window back at
100 kHz now fails: "only 16 blocks decoded (expected >=25)".

## The other callers, and why the bug was possible at all

Checked, and they were fine: the calibration, scan and drift paths in
`src/sdrprobe.c` all already passed 50 kHz. `gsm_sch_decode` was the only site
at 100 kHz. But that is the interesting part — the value was a bare `50000.0`
repeated at four call sites with nothing saying what it meant, so the one site
that disagreed looked no different from the ones that agreed.

Two changes so it cannot drift apart again:

- `GSM_FCCH_SEARCH_HALF_HZ` in `gsm_dsp.h`, used by every caller including the
  chain probe, with the reasoning at the definition: what the bound represents
  (how far the receiver may plausibly be off), what it is in ppm, and what
  happens if it is widened.
- `gsm_fcch_detect`'s parameter renamed `search_window_hz` →
  `search_half_width_hz`. "Window" reads as a full width, and passing 100 kHz
  meaning "±50 kHz" is exactly the mistake that was made. Note
  `sdr_dsp_estimate_channel_center` next door already names its equivalents
  `coarse_half_width_hz` / `fine_half_width_hz` — the FCCH detector was the odd
  one out.

The chain probe had also drifted: its STAGE 2 walkthrough passed 100 kHz of its
own, so the carrier it printed was not necessarily the one the library used.
A diagnostic that quietly disagrees with the code it documents is worse than no
diagnostic. It now uses the shared constant.

Same shape as the field-layout bug in [[06-frame-number-diagnosis]]: a stage
trusting an upstream result it never checked, with a duplicated definition
letting the copies disagree.
