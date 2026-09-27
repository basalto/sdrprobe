# 03 - What a change costs, by layer

Status: resolved, 2026-09-27 -- one change made, two options left open,
two candidate wins measured and rejected.

## The question

Can the gate cost less for a change that plainly cannot break most of it?
Asked after `.scratch/layer-boundaries/` put `src/` into seven layers.

## Where the time goes -- measured, this machine, 2026-09-27

**The gate is 148-151 s warm at the default `-j4`, and 223 s cold.** The
header said **57 s**, which was true of a 55-suite gate; there are 83 suites
now, 22 325 checks, two binaries instead of one, and a browser check.
`CLAUDE.md` is corrected.

Serial, the 84 suites total **307 s**, so `-j4` buys 2.0x rather than 4 --
the memory-bandwidth ceiling `CLAUDE.md` already records.

| | |
|---|---|
| `check-pipelines` | 46 s (50.6 s for `pipelines.sh` alone, both binaries warm) |
| `check-signal-probe` | 40.9 s -- **38.9 s running, 2.8 s compiling** |
| `check-web-layout` | 14.4 s (needs node + chromium) |
| `check-lte-chain-analysis` | 13.6 s |
| top five together | 127 s of the 307 s serial total |
| 40 of 84 suites | under 1 s each |
| `sdrprobe` / `sdrprobe-gui` builds | 28.6 s / 45.1 s |

**`check-touched` by layer**, against a 220 s gate at the time:

| changed | suites | time | share |
|---|---|---|---|
| `gui/view_fm.c` | 1 of 83 | 56 s | 25% |
| `runtime/frame_advance.c` | 2 | 67 s | 30% |
| `model/survey_view_model.h` | 3 | 78 s | 35% |
| `tech/gsm_dsp.c` | 4 | 101 s | 46% |
| `core/sdr_dsp.c` | 21 | 215 s | 98% |

**There is a ~50 s floor and it is `pipelines.sh`, not the build.** Any change
under `src/` pulls in `check-pipelines`, which runs the built program over
every capture. A `core/` change *is* the gate -- 21 suites, 98% -- so there
is nothing to pick there and `make check` is the honest command.

## Done: a `gui/` header no longer rebuilds the no-window binary

`APP_HDR` was one flat list and a prerequisite of both binaries, so editing
any of the 23 `gui/` headers recompiled `./sdrprobe` -- which **provably
cannot include one**, because `check-layers` refuses an include from
`CORE_SRC` up into `gui/` (ADR-0028).

Split into `CORE_HDR` (56 headers, at or below `server/`) and `GUI_HDR_APP`
(18), with `APP_HDR = CORE_HDR + GUI_HDR_APP` so every other rule is
unchanged. `./sdrprobe` takes `CORE_HDR` only.

**Measured on a `gui/` header change: 113.5 s of rebuild to 62.7 s.** A 45%
saving, and larger than the 28.6 s the `sdrprobe` build costs alone, because
the two builds had been competing for cores.

This is the layering paying for itself in a way that has nothing to do with
navigation: the dependency was always spurious, and only `check-layers` makes
"spurious" a fact a build rule may rely on.

## Measured and rejected

**`CHECK_JOBS` above 4.** Two passes in opposite directions, because a
monotone result measured in monotone order is how a warming effect fakes
one:

| | forward | reverse | mean |
|---|---|---|---|
| -j2 | 184.1 | 222.7 | 203 |
| -j4 | 148.1 | 150.6 | 149 |
| -j6 | 136.7 | 158.9 | 148 |
| -j8 | 136.5 | 145.7 | 141 |

The -j4 to -j8 gap is 6%; the spread *within* -j6 is 15%. Only -j2 is
clearly worse. `nproc/2` stays -- not because it wins, but because nothing
beats it by more than the noise. `CLAUDE.md`'s older claim that -j8 is
actively *worse* also no longer reproduces; at this gate size they are the
same.

**Ordering `CHECK_UNITS` longest-first.** It had genuinely decayed --
`check-lte-chain-analysis`, third-longest at 13.6 s, had been appended at
position 80 of 83 -- so this looked like the easy win.  Sorted by measured
time the gate read **158, 165 and 207 s** against a 148-151 s baseline. No
better, possibly worse. The pole is already first, and with 83 jobs in four
lanes `make -j` fills the tail by itself. Reverted.

**A near-miss worth recording.** The first attempt at that reordering used
`textwrap.wrap`, which breaks on hyphens, so `check-adsb-analysis` became
`check-adsb-` and `analysis`. The gate then ran **68 suites and reported "no
failures"** -- 19 230 checks instead of 22 325. It was caught only because
the suite count is printed and I was reading it for the timing. A rewrite of
`CHECK_UNITS` must diff the *set* before it is timed, which is what the
second attempt did.

## Left open, with what they would cost

**Split `check-signal-probe`.** 39 s of *running* in one process is a quarter
of the gate and the only real pole; nothing can finish faster than the
longest suite. Its 29 tests are independent, so splitting it into three or
four gated suites is mechanical. Worth perhaps 25 s of the 150. Not done
because it is the one change here that would need care about which
assertions belong together, and this ticket was an evaluation.

**Split `check-pipelines` by binary.** Its decode groups drive `./sdrprobe`
only; the "two binaries" group needs both. A `gui/`-only change cannot alter
a decode, so it could run the second group alone. Worth perhaps 35 s of a
`gui/` change's 56. Not done because `pipelines.sh` is one script with shared
setup and the split has to not weaken what it proves.

**Object caching (`.scratch/gate-time/` ticket 02) stays wontfix, and now
there is a number for it.** `check-signal-probe` is 2.8 s of compiling
against 38.9 s of running; the units are run-bound, so caching their
compilation buys almost nothing. It would only help the two binary builds --
and the header split above already took the worst case of that.
