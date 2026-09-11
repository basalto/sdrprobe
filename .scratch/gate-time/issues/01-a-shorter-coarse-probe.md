# 01 - The coarse scan does not need the full probe

Status: resolved, 2026-09-11

`signal_find_carrier()` costs `O(window / spacing * probe)` with
`spacing = rate / probe * 4`, which is `O(window * probe^2 / rate)`. The probe
is capped at 300 000 pairs, so a full-span search at 2 MS/s is ~75 000 grid
points each mixing 300 000 pairs.

A line's width in a length-N mix is about `rate/N`. So a coarse stage using
32 768 pairs may step at ~244 Hz rather than 26.7, which is nine times fewer
points each nine times cheaper -- about **80x** off the coarse stage -- with
the existing refinement then walking down to the same final resolution, and the
final measurement already taken at full length.

## What a prototype did, and why this is a ticket rather than a patch

Tried: coarse stage at 32 768 pairs, refinement starting at one coarse step and
narrowing as before.

**`check-signal-probe` went from 105 s to 3 s, and five checks failed.** Not
marginal ones -- the standing fraction collapsed, `0.0118` where `0.679` was
expected, which is what an off-frequency mix reads. So the coarse stage landed
on the wrong line in the synthetic cases, or the prototype's refinement window
did not recover the coarse error it now has to recover. Either way the answer
moved, and this function's answers are pinned on a real capture
(`carrier_75000_bare.bin`: +299 478.2 Hz, 47 dB over its floor, 0.923
standing) and consumed on air.

So it needs the `does-it-help` treatment rather than an edit:

- the refinement window must be at least one coarse step wide, or the coarse
  error is unrecoverable by construction;
- the coarse probe length is a constant to be **measured where it breaks**, not
  chosen -- the question is the shortest probe at which the weakest line in the
  corpus is still the winner of the coarse stage;
- every real-capture invariant has to come back identical, not close;
- and the win should be measured on air too, since the survey's confirmation
  pass pays this per candidate.

## What must be checkable

That a weak line is still found. The corpus has strong ones; the case this
optimisation can break is a line near the coarse stage's noise, and there is no
synthetic check for it today. That check is worth having **before** the change,
because it is the one that would fail.

## Comments

**Done 2026-09-11, and it was a defect rather than an optimisation.**

The prototype's five failures were not lost processing gain. Instrumenting the
coarse stage showed the magnitude **at the tone** was ~1.0 at every probe
length while the grid's own maximum was 0.13 -- the grid never evaluated a
point near the line. The step was `rate/probe * 4`, **four times the main lobe
of the mix it was probing with**, so a line one or three lobes off a grid
point sat in a null at *both* bracketing probes.

That is a comb of blind frequencies, not a corner case, and it is measurable
with no noise at all. On the shipped code, a clean tone:

```
120000 Hz  found exactly          120015 Hz  found
120005 Hz  found                  120020 Hz  found
120010 Hz  LOST, answered 58 kHz away
120030 Hz  LOST, answered 36 kHz away
```

Every synthetic fixture in `signal_probe_test.c` uses 120 000 Hz, which falls
exactly on the grid, and the real captures landed elsewhere. The comment
claiming the grid was "fine enough that a line cannot hide between two of
them" was false by a factor of four.

**The fix is the grid, and the speed is a consequence.** `rate/coarse` over a
`SIGNAL_COARSE_PAIRS` prefix: every frequency within half a lobe of a probe,
3.9 dB of scalloping at worst and never a null. Cost is
`window * coarse^2 / rate`, so a short coarse look pays for the finer grid
several times over.

**The constant was measured where it breaks.** Over 24 draws with the tone
placed anywhere in a 400 Hz span, under noise 30x its amplitude: 8192 loses 2,
16384 loses 1, 32768 and 65536 lose none. 32768 is still wrong, for a reason
the noise sweep cannot see -- a swept carrier has no line, the one the search
settles on reads 16.3 dB against a 15 dB bar, and a 16.4 ms look lands
marginally worse at 11.4 dB, turning "a modulated carrier" into "no carrier".
**65536** reproduces the shipped answer on that fixture to 0.1 dB.

**What it cost and bought:**

| | before | after |
| --- | --- | --- |
| `check-signal-probe` | 105 s | **30 s** |
| `make check` | 171 s | **115 s** |
| `probe-signal` over one capture | 13.0 s | **3.0 s** |
| a weak line anywhere in 400 Hz, noise 30 | 4 of 24 lost | **0 of 24** |
| the real capture, windowed | +299478.2, 47.1 dB, 0.929 | **identical** |

**One documented invariant was the bug's fingerprint and had to go.** The
capture's sidecar and `CLAUDE.md` both recorded that a whole-span search
returns "a real neighbour at +176 kHz at 38 dB" rather than the 47 dB carrier
beside it -- and `check-signal-probe` asserted it, as *"but it is a different
signal"* and *"a weaker one, so nothing flags the mistake"*. A whole-span
search now returns the strongest line, which is the target. The lesson that
assertion was reaching for is kept and made true: a search **windowed** on
140-210 kHz still returns the neighbour, confidently and with nothing in the
result to say so.

`test_a_line_is_found_wherever_it_sits()` walks a clean tone across a whole
old-grid step and is the check this ticket asked for. Verified against the
pre-fix source: it fails there, three offsets of nine, each 52-58 kHz out.
