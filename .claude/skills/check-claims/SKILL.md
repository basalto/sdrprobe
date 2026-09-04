---
name: check-claims
description: Write check assertions that hold, and read a failing one correctly. Use when adding to tests/, when a check you just wrote fails, or when deciding whether a failure means the code is wrong or the claim is.
---

# The claim is as likely to be wrong as the code

Checks here carry prose: `check_true("a scan of band II stays under a minute",
here < 60.0)`. The prose is a claim about the program, and a claim can be
false while the arithmetic beside it is impeccable.

**A new check that fails on its first run is more often a wrong claim than a
found bug.** Read it that way first. Six of the wrong ones below were written
in a single session, and every one cost a build-and-run cycle to discover.

## Read the failure before fixing the code

Ask, in this order:

1. **Is the claim true of the program as it is meant to work?** Not "does it
   sound right" -- work the case by hand. A drag entirely outside the received
   span sounds like it should clamp to the edge; the code falls back to the
   whole span, and falling back is correct, because a pan running off the end
   needs to keep drawing while the retune arrives.
2. **Am I asserting a property of a superset?** The Settings panel's caption
   check ran over every control. Two of them are checkboxes that label
   themselves to the right, and two are buttons carrying their text inside.
   Asking those for a caption asks about a rectangle nothing draws into.
3. **Is the bound the one I measured, or the one I hoped for?** A cost model
   charged at its cap is a worst case. `fm_scan_seconds()` at an eight-second
   cap put a band scan at 77 s while the measured run took 37, because the
   name pass stops early. Both numbers are true and they answer different
   questions.
4. **Only then: is the code wrong?**

Fixing the code to satisfy a false claim is the expensive mistake, because the
check then locks the wrong behaviour in and reads as authority.

## The wrong claims that actually got written

Each of these compiled, read plausibly, and was false.

- **A degenerate expression.** `f(13, 40, 7) - f(13, 40, 7)` is zero, not a
  comparison. When two calls differ only in an argument you meant to vary,
  read the arguments, not the shape.
- **Two things that cannot coexist, asserted not to collide.** The channel
  scan overlay's buttons sit exactly where the chrome's Settings button does.
  That is not a bug -- the overlay is drawn *instead of* the chrome and owns
  the input while it is up. The useful check asserts the overlap deliberately
  and says which way to read it, so the day the overlay starts drawing the
  chrome, it fails.
- **True of the measurement, false of the bound.** "The name pass is the
  smaller half" held for the run and not for the cap.
- **A property that holds for the wrong reason.** A dwell "narrower means
  longer" was false because the range has a flat ceiling and floor.

## What makes a claim worth having

- **Assert the property, not the value.** `check-layout` parses the version
  into three numbers rather than comparing it to `"v0.9.0"`, so a bump is not
  a check edit. It asserts the *last* help topic stays inside the panel rather
  than a row height, so the next topic added is caught, not just the next
  resize.
- **Assert the thing that would have caught the bug**, named in the comment.
  `sdrgui_waterfall_span()` exists because a zoom was silently discarded and
  nothing could ask what the chart would draw.
- **Assert the fact a design rests on.** `fm_scan_visit_fills_chunk()` states
  outright that a scan visit is shorter than one chunk. If the visit ever
  grows past one, whoever changed it finds out there rather than by deleting
  the flush that exists because of it.
- **Prefer a walked property to a spot value.** Whether a one-channel window
  always contains a channel centre is a question about phase, so the check
  walks a window across a whole channel in twentieths -- and confirms that
  half a channel sometimes contains none, which is the half that shows the
  floor is doing work.

## After a real-signal measurement, pin the number you saw

Not the number you expected. A measured 19 groups agreeing is worth recording;
a round 20 is not. If the honest number is marginal, say so in the sidecar or
the comment and widen the margin rather than asserting the marginal case --
`fm_rds_tsf.bin` went from two seconds to three because a name at two seconds
depended on where the segment cycle fell, and `check-pipelines` had been
passing on a coin flip.
