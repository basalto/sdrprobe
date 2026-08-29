# 04 — Multi-burst frame-number lock (optional, app layer)

Status: resolved
Blocked by: 02

Add an application-layer `struct gsm_sch_tracker` (keep `gsm_sch_decode()` pure,
ADR-0009 precedent):

- Vote the constant T1 across recent bursts; report the majority.
- Once two bursts agree, predict the next burst's frame number from the elapsed
  frames and accept a decode only if consistent, else fall back to the counter.

Add the `Frame-number lock` glossary term to `docs/contexts/decoder/CONTEXT.md`
when adopted. Independent robustness layer; directly cures the single-bit T1
symptom the probe shows. Detail: `docs/sch-frame-number-decode.md` §5.4.

## Comments
