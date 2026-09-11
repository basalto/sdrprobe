# 02 - The gate recompiles all 56 binaries every run

Status: **wontfix, 2026-09-11** -- measured at four seconds of a fifty-six second gate. Reopen if the note at the end comes true.

`make check` with nothing changed takes exactly as long as with everything
changed -- 242 s both, measured. Every `check-*` is a phony target, so make
rebuilds each suite's binary every time; ~96 `.c` compilations per run, with
`sdr_dsp.c` compiled fifteen times.

Two ways to fix it, with different risks.

**Real file targets.** Split each rule: `$(BUILD)/x_test` compiles, `check-x`
depends on it and runs it. A repeat gate then compiles nothing and only runs.
**The risk is the reason to think first**: today every prerequisite list is
harmless if incomplete, because everything recompiles anyway. Make compilation
conditional and an incomplete list means a *stale binary passing the gate* --
which is exactly the fault this repository has already had with `APP_HDR`,
where editing a header rebuilt its check and not the program, and a screenshot
came back showing wording that had already been changed. So this needs all 56
prerequisite lists audited against what each translation unit actually
includes, and that audit is the work, not the Makefile edit. Spot-checked,
they look well kept -- `check-lte-dsp` lists `lte_gold.h` and
`device_profile.h` -- which makes the audit plausible rather than hopeless.

**ccache.** Transparent, and it cannot go stale: it hashes the preprocessed
source and the flags, so an incomplete prerequisite list is not a correctness
problem, only a missed cache hit. It is **not installed on this machine**, so
this is a suggestion rather than a measurement -- and it wants a decision
recorded, because it makes the gate's speed depend on something outside the
repository, which nothing else here does.

Shared object files (`build/obj/sdr_dsp.o`) would cut the first run too, and
no check rule compiles a shared source with different flags -- the only `-D`
in the whole set is `probe-two-cell`'s, which is a probe. That makes it
possible; it is still a large rewrite of a heavily commented Makefile.

## What it is worth

At most ~60 s of the 171, and only on the first run of a session; after
ticket 01 it would be a larger share of a smaller number. Do 01 first.

## Comments

**2026-09-11, after the gate came down to 72 s. This ticket's premise has
weakened and should be re-read before anybody spends a day on it.**

When it was written the gate was 171 s and compilation was an obvious ~60 of
them. Three things have changed what those 60 seconds are worth:

- **`check-signal-probe` went from 54 s to 37**, so the pole is shorter and
  the compile work is a larger share of the *total* but a smaller share of the
  *critical path*.
- **The units are ordered longest-first** and **`check-pipelines` is in the
  pool**, so the run is packed rather than tailed.
- **`CHECK_JOBS` is `nproc/2`**, because these suites saturate memory
  bandwidth: measured, -j4 is 66 s where -j8 is 72 and -j16 is 78. Compilation
  is CPU work competing for the same bandwidth, so removing it helps less than
  the CPU-seconds suggest.

Together: of the 72 s the gate now takes, removing **all** compilation would
buy perhaps fifteen. That is a day of Makefile surgery and a 56-rule
prerequisite audit -- with a stale-binary failure mode -- for about 20%.

**The cheaper thing to do first is `check-pipelines`**, which is ~29 s of
serial shell running 21 program invocations that are independent of each
other. It overlaps the units now, so it is no longer on the critical path
either -- but it is the second-longest job, and if the pole ever gets shorter
it becomes the pole. Parallelising it means each section writing its own
report file and its own counts, then the parent summing them; the accounting
is the risk, and it is visible rather than silent because the suite's check
count is printed.

**If this ticket is done anyway, do it with generated dependencies, not an
audit.** `gcc -MMD -MP` per object records what the compiler actually read, so
an incomplete list stops being possible rather than being something a human
re-checks. Verified while investigating: a single compile-and-link command
**cannot** produce a correct dependency file -- `gcc -MMD -MF x.d -o prog a.c
b.c` overwrites `x.d` once per source and leaves only the last -- so it needs
real object files, one `.c` to one `.o` to one `.d`. Also verified: **no check
rule compiles a shared source with different flags** (the only `-D` anywhere
in the set belongs to `probe-two-cell`, a probe), so the objects can be shared
between suites. That removes the duplicate compilation as well as the repeat.

## Closed on the measurement, 2026-09-11

The gate is **56 s** now, and the units phase and its longest single suite are
almost the same number:

| | |
| --- | --- |
| `make check` | 56 s |
| the units phase at `-j4` | 49 s |
| `check-signal-probe`, run alone | **45 s** |
| `check-pipelines` | 35 s, entirely overlapped |

**Compilation is already hidden behind the pole.** The units take 49 s and one
process in them takes 45, so the other 57 suites *and* all ~200 translation
units fit in the four seconds of slack. Removing every compile in the gate --
a 58-rule Makefile rewrite, real object files, generated dependencies, and the
stale-binary failure mode to think about -- would buy **about four seconds of
fifty-six**.

That is not worth a day, and saying so needs the number rather than the
instinct. It cost three measurements to find out, and the estimate it replaces
was ~15 s, made when the gate was 171 s and the pole was 54.

For the record, since the next person will ask: the pole is not one greedy
test any more. `check-signal-probe` profiles flat -- 10.7 s, 9.0, 4.7, 4.6,
3.1, 3.1, 2.9, 2.2 -- about forty seconds of genuine carrier searches over
200 000- and 400 000-pair buffers, after the coarse-grid fix in ticket 01 took
it from 105 s. There is no 10x left in it, and trimming buffer lengths would
be trimming claims: several of those tests exist *because* the answer depends
on the length of the look.

## What would reopen this

Compilation becomes the floor the moment the pole stops being one. Either:

- **`check-signal-probe` gets faster or is split.** Splitting it into two
  suites would halve the pole for free -- no claim changes, only which binary
  runs which tests -- at the cost of one subject having two names, which this
  repository's suites do not otherwise do. That trade is worth reconsidering
  if the gate ever needs to be under thirty seconds.
- **Another long suite appears.** `CHECK_UNITS` is ordered longest-first and
  the Makefile carries the one-liner that re-measures it; a new 45 s suite
  makes the slack disappear and this ticket worth its day.

Everything verified while investigating is still in the notes above and still
true: a single compile-and-link command cannot produce a correct dependency
file, and no check rule compiles a shared source with different flags.
