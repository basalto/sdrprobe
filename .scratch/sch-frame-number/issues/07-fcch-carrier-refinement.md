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

## Note

Same shape as the field-layout bug in [[06-frame-number-diagnosis]]: a stage
trusting an upstream result it never checked. Worth asking of the other
consumers of `gsm_fcch_detect` — the calibration and drift paths in
`src/sdrprobe.c` — whether a tone tens of kHz from expectation should be
accepted there either, or at least surfaced.
