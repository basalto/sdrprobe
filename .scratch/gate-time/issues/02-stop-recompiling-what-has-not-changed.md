# 02 - The gate recompiles all 56 binaries every run

Status: needs-triage

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
