# 06 — Wiring, keys and documentation

Status: resolved
Blocked by: 05

- `enum view_kind` gains the survey; key `5` selects it, and the Scope option
  row lists it.
- `--view survey` in options.c, and `--survey-range LOW:HIGH` to set the range
  from the command line. Headless is out of scope: the survey is a thing you
  look at.
- The help overlay gains a Band survey topic: what the sweep measures, what a
  candidate is, why the band plan is a lookup and not an identification, and
  how to read duty and stability.
- `docs/contexts/decoder/CONTEXT.md` is untouched -- this is Probe. `CONTEXT.md`
  gains the five terms in the spec, and its **Channel power scan** entry stops
  forbidding the word "survey" and points at the new term instead.
- AGENTS.md, README.md, `docs/sdrprobe-implementation.md`.
- An ADR is warranted: the band plan is the first table in this program that
  attaches meaning to a frequency, and the line between "this is what the band
  is for" and "this is what the signal is" is exactly the kind of thing a later
  reader will blur. Write it as `docs/adr/0011-band-plan-is-a-lookup.md`.

## Comments
