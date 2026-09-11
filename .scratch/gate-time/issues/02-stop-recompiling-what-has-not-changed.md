# 02 - The gate recompiles all 56 binaries every run

Status: needs-triage -- and worth less than it looked, see below

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
