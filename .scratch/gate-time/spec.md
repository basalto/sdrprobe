# The gate takes three minutes, and one suite is most of it

## What was measured, 2026-09-11

An eight-core machine, `-O3`, gcc, warm `build/`:

| | |
| --- | --- |
| `make check`, serial | **242 s** |
| the same again, nothing changed | **242 s** |
| `make -j8 check` | **172 s** |
| `make check` with `CHECK_JOBS` (now the default) | **171 s** |

The second row is the first finding: **nothing is cached between runs.** Every
`check-*` is a phony name, so make can never consider one up to date and
rebuilds all 56 binaries every time. Across the rules there are ~96 `.c`
compilations and `sdr_dsp.c` alone is compiled fifteen times.

Per-suite **run** time, binaries already built, slowest first:

```
105087 ms  signal_probe_test        <- the gate
  6764 ms  lte_mib_test
  6421 ms  tetra_session_test
  5268 ms  lte_dsp_test
  4923 ms  fm_session_test
  4894 ms  fm_dsp_test
  2854 ms  gsm_session_test
```

`check-pipelines` is 34 s. Everything else runs in under 7 s. So the 171 s is
roughly: 105 s of one suite, 34 s of pipelines, ~60 s of compilation and ~40 s
of every other suite, with the last two divided by eight and the first two not
divisible at all.

**Parallelism is done** and is the whole of the easy win: `CHECK_JOBS` defaults
to `nproc`, `--output-sync=target` keeps the report readable, and a deliberately
failed suite still fails the gate with its message intact (verified, exit 2).
That took 242 to 171 and cannot do better, because 105 of those seconds are one
process.

## Why that suite is slow, and why it is not the suite's fault

`signal_find_carrier()` scans coarsely and then refines. The coarse step is
`sample_rate / probe * 4`, and `probe` is `min(pair_count, 300000)` -- so at
2 MS/s the coarse grid is **26.7 Hz** and every one of its points evaluates
`line_magnitude()` over **300 000 pairs**. A full-span search is then about
75 000 evaluations of 300 000 pairs, which is the 40 s that
`test_a_real_bare_carrier` spends on one assertion -- the deliberate one, where
searching the whole span instead of a window returns the real neighbour at
+176 kHz and proves the window is the caller's responsibility.

The function's own comment says "a full-length scan of every candidate
frequency is the thing that does not finish", so the shape was known; what is
not exploited is that the *coarse* stage does not need the full probe. A line's
width in a length-N mix is about `rate/N`, so a shorter probe permits a
proportionally coarser grid, and the saving is quadratic.

**This is not only a test cost.** The survey's confirmation pass measures every
candidate through this function, and `survey_measure_settled()` does it per
candidate on air.

## Order

1. `01` -- a shorter coarse probe, under `does-it-help`.
2. `02` -- stop recompiling what has not changed.
