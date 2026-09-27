# 04 - Two binaries, one job each

Status: resolved, 2026-09-27
Blocked by: 03 (done)

## The problem

After `.scratch/layer-boundaries/issues/04-*` there were two binaries and the
names were backwards:

- `sdrprobe` -- the window, *and* `headless`, *and* `web`
- `sdrprobe-server` -- `headless` and `web`, no raylib

Three things wrong with that. The plain name went to the build that needs a
graphics stack, so **`make` failed outright on a machine that was never going
to open a window**. `sdrprobe-server headless --decode` read oddly, since the
binary is not a server -- it is the no-window build, and `headless` is not
serving anything. And both binaries accepted both modes, so a script could
land on either and quietly work, which is only fine until the two are not
interchangeable.

## What changed

The plain name follows the usage, and each binary does one job:

    sdrprobe headless [flags]   no window and no link; prints to stdout
    sdrprobe web [flags]        serves the browser Viewer, opens one at it
    sdrprobe-gui [flags]        the window, and nothing else

**The direction was chosen on a count, not a feeling.** Of the invocations in
this repository's own living docs and scripts, **44 are `headless` or `web`
and 18 are windowed** -- so inverting the names left three quarters of them
correct for free and moved eighteen.

`server` is gone as a command word. It was `web --no-browser`, so it is the
flag.

**`sdrprobe-gui` refuses `headless` and `web`**, and the two refusals are not
the same kind of thing -- which the code says outright, because the asymmetry
is the interesting part:

- `sdrprobe` has no window because **raylib is not linked into it**. A fact
  about the build.
- `sdrprobe-gui` *could* run `headless` -- it links the same `CORE_SRC` --
  and declines anyway. A **policy**, so that each binary does one job and a
  script cannot land on the wrong one and quietly work.

Both messages name the *other* binary, because "wrong build" only helps a
reader who is told which one is right.

## Two things the rename forced that are improvements on their own

**`usage()` takes `has_window`.** The two builds have different command
surfaces, so one help text would lie to whichever binary it was not written
for. It is passed rather than derived from `argv[0]`, which a rename, a
symlink or an install path would make a lie.

**The command descriptions moved under their commands.** They were a column,
aligned by counting the characters in `"./sdrprobe"` -- so `sdrprobe-gui`,
four characters longer, pushed every continuation line out of true. A line
nobody has to count is a line nobody breaks.

## What was given up, and why that is acceptable

`check-pipelines` had a group that ran a GSM decode, an FM decode and a
capture survey through **both** binaries and required the output
byte-identical. It was the check that proved dropping `VIEW_SRC` changed no
answer, and it is impossible once the GUI build refuses to decode.

It was a **migration** check. Both binaries are built from the same
`CORE_SRC` objects, so a divergence now would need the same source to compile
two ways. The group is replaced by the new contract: `./sdrprobe` decodes, a
bare `./sdrprobe` says it has no window, `./sdrprobe-gui` refuses both modes
naming the other binary, and `server` is refused **by name**.

That last one matters more than it looks: `server` is the word most likely to
be typed from memory, and a word that silently became something else would be
worse than one that refuses.

## The version call

**0.64.0 -- MINOR, and it breaks things on purpose.** Three invocations stop
working: bare `sdrprobe`, `sdrprobe server`, and windowed `sdrprobe --view
...`. Each fails **loudly and by name**, which is what made the break
acceptable; the failure it was weighed against is the silent one, a
`sdrprobe --view fm` quietly doing nothing on a box with no display. MINOR
rather than MAJOR only because of the leading zero, which says exactly this
may happen; at 1.0.0 it would be MAJOR. `src/version.h` carries the
reasoning.

## Source files are not renamed

`src/sdrprobe.c` builds `sdrprobe-gui`, which is a mismatch, and it stays:
that file is named in ~40 living places including six ADRs, and an ADR is a
record of a decision as taken. Only `server_main.c` followed its binary, to
`sdrprobe_main.c`, because it had two references. Source file names need not
match binary names, and paying forty edits for that would be paying for
tidiness with history.

## Related

`.scratch/one-receiver-many-clients/spec.md` came out of the same
conversation: one process owning the receiver with the window, the browser
and scripts as clients. Evaluated, not started, with the four blockers
measured -- and with the operator's constraint recorded that **`headless`
never becomes a socket client**. Nothing decided here forecloses it.
