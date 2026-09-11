# The gate takes three minutes, and one suite is most of it

## What was measured, 2026-09-11

An eight-core machine, `-O3`, gcc, warm `build/`:

| | |
| --- | --- |
| `make check`, serial | **242 s** |
| the same again, nothing changed | **242 s** |
| `make -j8 check` | **172 s** |
| `make check` with `CHECK_JOBS` (now the default) | **171 s** |
| after fixing `signal_find_carrier` (ticket 01) | **115 s** |
| after ordering `CHECK_UNITS` longest-first | **95 s** |
| with `CHECK_JOBS` = nproc/2 and pipelines in the pool | **72 s** |

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

1. `01` -- a shorter coarse probe, under `does-it-help`. **Done 2026-09-11**,
   and it turned out to be a defect: the coarse grid was four times the main
   lobe it was probing with, so it had a comb of blind frequencies at which a
   noise-free carrier was lost outright. The gate is **115 s** now and
   `probe-signal` went from 13.0 s to 3.0 s over one capture.
2. `02` -- stop recompiling what has not changed. **Re-read its comments
   before starting**: at 72 s the compilation is mostly off the critical path
   and removing all of it would buy about fifteen seconds.

## What the scheduling turned out to be worth

Two of the four wins were scheduling rather than work, and both were invisible
until the phases were timed separately:

- **`make -j` starts targets in list order**, so the 54 s suite sitting late
  in `CHECK_UNITS` added its tail to the end of the run. The units phase took
  74 s against a floor of 54; ordering longest-first closed most of that.
- **`check-pipelines` was a serial phase after the units**, so its 29 s was
  pure addition: 66 + 29 = 95. As one more job in the same pool, started
  first, the pair takes 70.

And one measurement contradicted the obvious default: **more jobs is not
faster past four.** The units read -j2 88 s, -j3 71, -j4 66, -j5 68, -j8 72,
-j12 75, -j16 78, repeatably. These suites stream large float arrays; they run
out of memory bandwidth long before they run out of cores.
